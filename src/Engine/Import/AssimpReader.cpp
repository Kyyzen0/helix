#include "Engine/Import/AssimpSkeleton.h"
#include "Engine/Import/ImageDecode.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Import/Readers.h"
#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <vector>

namespace helix::import {

namespace {

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

// What reading the textures of one file needs, and what it has decoded so far, so a picture
// several materials share is decoded once.
struct TextureContext
{
    const aiScene& scene;
    std::filesystem::path modelFile;
    std::filesystem::path folder;
    // Where the files a texture needs are added, for the import cache. Null when not wanted.
    std::vector<std::filesystem::path>* opened = nullptr;
    std::map<std::string, DecodeResult> decoded;
};

// The percent escapes of a path written as a URI ("my%20picture.png").
std::string decodePercent(const std::string& text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2])))
        {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

// The file a texture path names, looked for next to the model, then under its own (absolute)
// path, then by its file name next to the model. Empty when none exists. Every path that was
// tried and is not a file is added to the list given, so the import cache can see one appear.
std::filesystem::path locateTexture(
    const TextureContext& context,
    const std::string& written,
    std::vector<std::filesystem::path>& tried
)
{
    std::string path = written;
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> spellings{path};
    const std::string plain = decodePercent(path);
    if (plain != path) spellings.push_back(plain);

    std::error_code error;
    const auto isFile = [&](const std::filesystem::path& candidate) {
        if (std::filesystem::is_regular_file(candidate, error)) return true;
        if (std::find(tried.begin(), tried.end(), candidate) == tried.end()) tried.push_back(candidate);
        return false;
    };
    for (const std::string& spelling : spellings)
    {
        const std::filesystem::path candidate(spelling);
        if (candidate.is_relative() && isFile(context.folder / candidate)) return context.folder / candidate;
        if (candidate.is_absolute() && isFile(candidate)) return candidate;
    }
    for (const std::string& spelling : spellings)
    {
        const std::filesystem::path name = std::filesystem::path(spelling).filename();
        if (!name.empty() && isFile(context.folder / name)) return context.folder / name;
    }
    return {};
}

DecodeResult decodeEmbedded(const aiTexture& texture, const std::string& source)
{
    if (texture.mHeight == 0)
        return decodeImage(texture.pcData, texture.mWidth, source);

    // Raw texels, blue first.
    const std::size_t count = std::size_t(texture.mWidth) * texture.mHeight;
    auto image = std::make_shared<geo::Image>();
    image->width = texture.mWidth;
    image->height = texture.mHeight;
    image->source = source;
    image->rgba.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        const aiTexel& texel = texture.pcData[i];
        image->rgba[4 * i] = texel.r;
        image->rgba[4 * i + 1] = texel.g;
        image->rgba[4 * i + 2] = texel.b;
        image->rgba[4 * i + 3] = texel.a;
    }
    DecodeResult result;
    result.image = std::move(image);
    return result;
}

// Loads the base colour texture of a material into it. A texture that cannot be loaded leaves
// the material with its factor and a note, and never fails the import.
void readTexture(const aiMaterial& source, TextureContext& context, RawMaterial& material)
{
    aiString path;
    aiTextureType type = aiTextureType_BASE_COLOR;
    if (source.GetTexture(type, 0, &path) != aiReturn_SUCCESS)
    {
        type = aiTextureType_DIFFUSE;
        if (source.GetTexture(type, 0, &path) != aiReturn_SUCCESS) return;
    }
    const std::string written = path.C_Str();
    material.hasTexture = true;

    std::string key;
    DecodeResult* result = nullptr;
    auto remember = [&](const std::string& id, auto&& decode) {
        key = id;
        auto found = context.decoded.find(key);
        if (found == context.decoded.end()) found = context.decoded.emplace(key, decode()).first;
        result = &found->second;
    };

    const aiTexture* embedded = written.empty() ? nullptr : context.scene.GetEmbeddedTexture(written.c_str());
    if (embedded)
    {
        const std::string id = written.front() == '*' ? written.substr(1) : written;
        remember("embedded:" + id, [&] {
            return decodeEmbedded(*embedded, context.modelFile.string() + "#embedded:" + id);
        });
    }
    else
    {
        std::vector<std::filesystem::path> tried;
        const std::filesystem::path file = locateTexture(context, written, tried);
        if (file.empty())
        {
            // The cache watches the places the picture could appear in, so copying it next to the
            // model later makes the file read again.
            if (context.opened)
                for (const std::filesystem::path& candidate : tried)
                    if (std::find(context.opened->begin(), context.opened->end(), candidate) ==
                        context.opened->end())
                        context.opened->push_back(candidate);
            material.missingTexture = "texture \"" + written.substr(0, 200) + "\" not found next to the model";
            material.missingTextureName = written.substr(0, 200);
            material.missingTextureNotFound = true;
            return;
        }
        remember("file:" + file.string(), [&] { return decodeImageFile(file); });
        if (context.opened &&
            std::find(context.opened->begin(), context.opened->end(), file) == context.opened->end())
            context.opened->push_back(file);
    }

    if (result->image)
        material.texture = result->image;
    else
    {
        material.missingTexture = "texture \"" + written.substr(0, 200) + "\" could not be loaded: " + result->error;
        material.missingTextureName = written.substr(0, 200);
    }
}

RawMaterial readMaterial(const aiMaterial& source, TextureContext& context)
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
    if (material.hasTexture) readTexture(source, context, material);
    return material;
}

// Assimp's own file access, with a note of every file it opens for reading. A glTF reads its
// buffers and a format may read its textures through this, and the import cache needs to know
// them to see that one of them changed while the main file did not.
class RecordingIOSystem : public Assimp::IOSystem
{
  public:
    explicit RecordingIOSystem(std::vector<std::filesystem::path>& opened) : opened_(opened) {}

    bool Exists(const char* path) const override { return inner_.Exists(path); }
    char getOsSeparator() const override { return inner_.getOsSeparator(); }
    bool ComparePaths(const char* one, const char* two) const override { return inner_.ComparePaths(one, two); }

    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override
    {
        Assimp::IOStream* stream = inner_.Open(path, mode);
        if (stream && mode && std::string(mode).find('r') != std::string::npos) opened_.emplace_back(path);
        return stream;
    }

    void Close(Assimp::IOStream* stream) override { inner_.Close(stream); }

  private:
    Assimp::DefaultIOSystem inner_;
    std::vector<std::filesystem::path>& opened_;
};

} // namespace

RawScene readWithAssimp(const std::filesystem::path& file, std::vector<std::filesystem::path>* openedFiles)
{
    const std::string extension = lower(file.extension().string());

    std::vector<std::filesystem::path> opened;
    Assimp::Importer importer;
    // The importer owns the file access it is given. It outlives every read below.
    if (openedFiles) importer.SetIOHandler(new RecordingIOSystem(opened));
    // The animations of an fbx are not read, so an fbx comes in with its skeleton and skin and
    // with no clip. Turning pivots into plain transforms is left off for the same reason: the
    // clips of an fbx have not been checked against this reader.
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

    // The file and everything it references have been read by now.
    if (openedFiles)
    {
        std::error_code error;
        for (const std::filesystem::path& path : opened)
        {
            if (std::filesystem::equivalent(path, file, error)) continue;
            if (std::find(openedFiles->begin(), openedFiles->end(), path) == openedFiles->end())
                openedFiles->push_back(path);
        }
    }

    RawScene result;
    result.animationCount = scene->mNumAnimations;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m)
        if (scene->mMeshes[m]->mNumBones > 0) ++result.skinnedMeshCount;
    result.declaredUp = declaredUpAxis(*scene);

    unsigned flags = aiProcess_SortByPType | aiProcess_ValidateDataStructure |
                     aiProcess_RemoveComponent;
    if (needsWelding(extension)) flags |= aiProcess_JoinIdenticalVertices;
    // Cameras and lights are not used, so they are dropped before they cost memory. The skins and
    // animations stay, they are read below. So do the embedded pictures: the base colour textures
    // are decoded from them.
    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS);
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

    TextureContext textures{*scene, file, file.parent_path(), openedFiles, {}};

    // Assimp adds a default material to most scenes. Only the materials a kept mesh uses are
    // listed, and the meshes are renumbered to match.
    std::vector<int> materialOf(scene->mNumMaterials, -1);
    auto useMaterial = [&](unsigned sourceIndex) -> int {
        if (sourceIndex >= scene->mNumMaterials) return -1;
        if (materialOf[sourceIndex] < 0)
        {
            materialOf[sourceIndex] = static_cast<int>(result.materials.size());
            result.materials.push_back(readMaterial(*scene->mMaterials[sourceIndex], textures));
        }
        return materialOf[sourceIndex];
    };

    // The joints of every bone of the file, and what the clips say about them.
    SkeletonReader skeleton(*scene);

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

        skeleton.readSkin(source, mesh);

        meshOf[m] = static_cast<int>(result.meshes.size());
        result.meshes.push_back(std::move(mesh));
    }

    // A skeleton no kept mesh is skinned to has nothing to move: the file is read as plain geometry.
    const bool anySkin = std::any_of(result.meshes.begin(), result.meshes.end(), [](const RawMesh& mesh) {
        return !mesh.skinJoints.empty();
    });
    if (anySkin)
    {
        skeleton.readClips();
        result.skeleton = skeleton.take();
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
        const Matrix4 world = current.parent * assimpMatrix(current.node->mTransformation);
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
