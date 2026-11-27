#include "Engine/Import/ImportFile.h"
#include "Engine/Import/Readers.h"
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace helix::import {

namespace {

Matrix4 toMatrix(const aiMatrix4x4& m)
{
    Matrix4 matrix;
    matrix << m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2,
        m.d3, m.d4;
    return matrix;
}

// STL is a list of triangles that name their corners one by one, so its corners are welded to
// get the shared points a mesh is edited with. Formats that already index their points are not.
bool needsWelding(const std::string& extension) { return extension == ".stl"; }

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

UpAxis declaredUpAxis(const aiScene& scene)
{
    if (!scene.mMetaData) return UpAxis::Auto;
    int axis = 1;
    int sign = 1;
    if (scene.mMetaData->Get("UpAxis", axis))
    {
        scene.mMetaData->Get("UpAxisSign", sign);
        // A file that points the other way along the axis is not one the import turns around.
        return axis == 2 && sign > 0 ? UpAxis::Z : UpAxis::Y;
    }
    return UpAxis::Auto;
}

RawMaterial readMaterial(const aiMaterial& source)
{
    RawMaterial material;
    aiString name;
    if (source.Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS) material.name = name.C_Str();

    aiColor4D color;
    if (source.Get(AI_MATKEY_BASE_COLOR, color) == aiReturn_SUCCESS ||
        source.Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
        material.baseColor = {color.r, color.g, color.b, color.a};

    material.hasTexture = source.GetTextureCount(aiTextureType_BASE_COLOR) > 0 ||
                          source.GetTextureCount(aiTextureType_DIFFUSE) > 0;
    return material;
}

} // namespace

RawScene readWithAssimp(const std::filesystem::path& file)
{
    const std::string extension = lower(file.extension().string());

    Assimp::Importer importer;
    // The animations of an fbx are not read at all, and no mesh is turned into a skeleton.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    // A file with a million triangles is one mesh. Assimp's default would split it at 1M vertices.
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 0x7fffffff);

    const aiScene* scene = nullptr;
    try
    {
        // Read first with no post processing, so what the file holds that is left out can be
        // counted before it is dropped.
        scene = importer.ReadFile(file.string().c_str(), 0);
    }
    catch (const std::exception& error)
    {
        throw ImportError(std::string("The file could not be read: ") + error.what());
    }
    if (!scene)
    {
        std::string reason = importer.GetErrorString();
        if (reason.size() > 300) reason.resize(300);
        throw ImportError("The file could not be read" + (reason.empty() ? "." : ": " + reason));
    }

    RawScene result;
    result.animationCount = scene->mNumAnimations;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m)
        if (scene->mMeshes[m]->mNumBones > 0) ++result.skinnedMeshCount;
    result.declaredUp = declaredUpAxis(*scene);

    unsigned flags = aiProcess_SortByPType | aiProcess_ValidateDataStructure |
                     aiProcess_RemoveComponent;
    if (needsWelding(extension)) flags |= aiProcess_JoinIdenticalVertices;
    // Skins, animations, cameras, lights and embedded pictures are not used, so they are dropped
    // before they cost memory.
    importer.SetPropertyInteger(
        AI_CONFIG_PP_RVC_FLAGS,
        aiComponent_ANIMATIONS | aiComponent_CAMERAS | aiComponent_LIGHTS | aiComponent_TEXTURES |
            aiComponent_BONEWEIGHTS
    );
    try
    {
        scene = importer.ApplyPostProcessing(flags);
    }
    catch (const std::exception& error)
    {
        throw ImportError(std::string("The file is damaged: ") + error.what());
    }
    if (!scene || !scene->mRootNode)
    {
        std::string reason = importer.GetErrorString();
        if (reason.size() > 300) reason.resize(300);
        throw ImportError("The file is damaged" + (reason.empty() ? "." : ": " + reason));
    }

    // Assimp adds a default material to most scenes. Only the materials a kept mesh uses are
    // listed, and the meshes are renumbered to match.
    std::vector<int> materialOf(scene->mNumMaterials, -1);
    auto useMaterial = [&](unsigned sourceIndex) -> int {
        if (sourceIndex >= scene->mNumMaterials) return -1;
        if (materialOf[sourceIndex] < 0)
        {
            materialOf[sourceIndex] = static_cast<int>(result.materials.size());
            result.materials.push_back(readMaterial(*scene->mMaterials[sourceIndex]));
        }
        return materialOf[sourceIndex];
    };

    // Assimp meshes that survive the sort hold one kind of primitive each.
    std::vector<int> meshOf(scene->mNumMeshes, -1);
    for (unsigned m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh& source = *scene->mMeshes[m];
        if ((source.mPrimitiveTypes & (aiPrimitiveType_TRIANGLE | aiPrimitiveType_POLYGON)) == 0 ||
            source.mNumVertices == 0 || source.mNumFaces == 0)
        {
            result.skippedPrimitiveCount += source.mNumFaces;
            continue;
        }

        RawMesh mesh;
        mesh.name = source.mName.C_Str();
        mesh.material = useMaterial(source.mMaterialIndex);

        const unsigned vertexCount = source.mNumVertices;
        mesh.positions.resize(3 * size_t(vertexCount));
        for (unsigned v = 0; v < vertexCount; ++v)
        {
            mesh.positions[3 * v] = source.mVertices[v].x;
            mesh.positions[3 * v + 1] = source.mVertices[v].y;
            mesh.positions[3 * v + 2] = source.mVertices[v].z;
        }
        if (source.HasNormals())
        {
            mesh.normals.resize(3 * size_t(vertexCount));
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                mesh.normals[3 * v] = source.mNormals[v].x;
                mesh.normals[3 * v + 1] = source.mNormals[v].y;
                mesh.normals[3 * v + 2] = source.mNormals[v].z;
            }
        }
        if (source.HasTextureCoords(0))
        {
            mesh.uvs.resize(2 * size_t(vertexCount));
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                mesh.uvs[2 * v] = source.mTextureCoords[0][v].x;
                mesh.uvs[2 * v + 1] = source.mTextureCoords[0][v].y;
            }
        }
        if (source.HasVertexColors(0))
        {
            mesh.colors.resize(3 * size_t(vertexCount));
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                mesh.colors[3 * v] = source.mColors[0][v].r;
                mesh.colors[3 * v + 1] = source.mColors[0][v].g;
                mesh.colors[3 * v + 2] = source.mColors[0][v].b;
            }
        }

        mesh.faceCounts.reserve(source.mNumFaces);
        mesh.indices.reserve(size_t(source.mNumFaces) * 3);
        for (unsigned f = 0; f < source.mNumFaces; ++f)
        {
            const aiFace& face = source.mFaces[f];
            if (face.mNumIndices < 3) continue;
            mesh.faceCounts.push_back(face.mNumIndices);
            mesh.indices.insert(mesh.indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
        }

        meshOf[m] = static_cast<int>(result.meshes.size());
        result.meshes.push_back(std::move(mesh));
    }

    // The hierarchy, from the root down, so every mesh knows where it sits.
    struct Pending
    {
        const aiNode* node;
        Matrix4 parent;
    };
    std::vector<Pending> pending{{scene->mRootNode, Matrix4::Identity()}};
    while (!pending.empty())
    {
        const Pending current = pending.back();
        pending.pop_back();
        const Matrix4 world = current.parent * toMatrix(current.node->mTransformation);
        for (unsigned i = 0; i < current.node->mNumMeshes; ++i)
        {
            const unsigned sourceIndex = current.node->mMeshes[i];
            if (sourceIndex < meshOf.size() && meshOf[sourceIndex] >= 0)
                result.instances.push_back({static_cast<std::size_t>(meshOf[sourceIndex]), world});
        }
        // Pushed last to first, so the first child is the first one visited.
        for (unsigned i = current.node->mNumChildren; i-- > 0;)
            pending.push_back({current.node->mChildren[i], world});
    }
    return result;
}

} // namespace helix::import
