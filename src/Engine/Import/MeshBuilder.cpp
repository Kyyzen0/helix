#include "Engine/Import/MeshBuilder.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Primitives/Mesh.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace helix::import {

namespace {

// The rotation that turns a Z up file into the Y up the engine uses: z goes to y and y to -z.
Matrix4 zUpToYUp()
{
    Matrix4 matrix = Matrix4::Identity();
    matrix(1, 1) = 0.f;
    matrix(1, 2) = 1.f;
    matrix(2, 1) = -1.f;
    matrix(2, 2) = 0.f;
    return matrix;
}

// The geometry of one output mesh, gathered from instances before it goes into a Mesh.
struct Gathered
{
    std::string name;
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    // True for a point whose mesh carried no normals; toMesh fills those in from the faces.
    std::vector<bool> normalMissing;
    std::vector<Vector3> uvs;
    std::vector<Vector3> colors;
    std::vector<Offset> faceCounts;
    std::vector<Offset> indices;
    std::vector<intT> faceMaterials;
    // The four joints and weights of every point. A point of a mesh that is not skinned has
    // weight 0 on all of them, which leaves it where it is.
    std::vector<std::array<intT, attr::names::skinInfluenceCount>> skinJoints;
    std::vector<std::array<float, attr::names::skinInfluenceCount>> skinWeights;
    bool hasSkin = false;
    bool hasNormals = false;
    bool hasUvs = false;
    bool hasColors = false;
};

// Appends one instance of a mesh, with its transform applied.
void gather(Gathered& out, const RawMesh& mesh, const Matrix4& world)
{
    const std::size_t vertexCount = mesh.positions.size() / 3;
    if (vertexCount == 0 || mesh.faceCounts.empty()) return;

    const Offset base = out.positions.size();
    const Matrix3 linear = world.topLeftCorner<3, 3>();
    const Vector3 translation = world.col(3).head<3>();
    // Normals follow the inverse transpose, which keeps them square to the surface under a
    // stretch. A matrix that cannot be inverted (a flattened node) leaves them as they were.
    const bool invertible = std::abs(linear.determinant()) > 1e-12f;
    const Matrix3 normalMatrix = invertible ? Matrix3(linear.inverse().transpose()) : Matrix3(linear);
    const bool mirrored = linear.determinant() < 0.f;

    const bool hasNormals = mesh.normals.size() == mesh.positions.size();
    const bool hasUvs = mesh.uvs.size() / 2 == vertexCount;
    const bool hasColors = mesh.colors.size() == mesh.positions.size();

    // A mesh in a merge that lacks what the others carry gets neutral values, so every
    // attribute stays as long as the points. The flags below say whether any mesh had it.
    out.hasNormals = out.hasNormals || hasNormals;
    out.hasUvs = out.hasUvs || hasUvs;
    out.hasColors = out.hasColors || hasColors;

    out.positions.reserve(base + vertexCount);
    for (std::size_t v = 0; v < vertexCount; ++v)
    {
        const Vector3 position(mesh.positions[3 * v], mesh.positions[3 * v + 1], mesh.positions[3 * v + 2]);
        out.positions.push_back(linear * position + translation);
    }
    out.normals.resize(base + vertexCount, Vector3::Zero());
    out.normalMissing.resize(base + vertexCount, !hasNormals);
    out.uvs.resize(base + vertexCount, Vector3::Zero());
    out.colors.resize(base + vertexCount, Vector3::Ones());

    constexpr int kInfluences = attr::names::skinInfluenceCount;
    out.skinJoints.resize(base + vertexCount, {});
    out.skinWeights.resize(base + vertexCount, {});
    if (mesh.skinJoints.size() == kInfluences * vertexCount && mesh.skinWeights.size() == kInfluences * vertexCount)
    {
        out.hasSkin = true;
        for (std::size_t v = 0; v < vertexCount; ++v)
            for (int i = 0; i < kInfluences; ++i)
            {
                out.skinJoints[base + v][i] = static_cast<intT>(mesh.skinJoints[kInfluences * v + i]);
                out.skinWeights[base + v][i] = mesh.skinWeights[kInfluences * v + i];
            }
    }

    for (std::size_t v = 0; v < vertexCount; ++v)
    {
        if (hasNormals)
        {
            Vector3 normal = normalMatrix * Vector3(mesh.normals[3 * v], mesh.normals[3 * v + 1], mesh.normals[3 * v + 2]);
            const float length = normal.norm();
            if (length > 1e-20f) normal /= length;
            out.normals[base + v] = normal;
        }
        if (hasUvs) out.uvs[base + v] = Vector3(mesh.uvs[2 * v], mesh.uvs[2 * v + 1], 0.f);
        if (hasColors)
            out.colors[base + v] = Vector3(mesh.colors[3 * v], mesh.colors[3 * v + 1], mesh.colors[3 * v + 2]);
    }

    out.faceCounts.reserve(out.faceCounts.size() + mesh.faceCounts.size());
    out.indices.reserve(out.indices.size() + mesh.indices.size());
    out.faceMaterials.insert(out.faceMaterials.end(), mesh.faceCounts.size(), static_cast<intT>(mesh.material));

    std::size_t cursor = 0;
    for (const std::uint32_t count : mesh.faceCounts)
    {
        out.faceCounts.push_back(count);
        for (std::uint32_t corner = 0; corner < count; ++corner)
        {
            // A mirror turns the faces inside out, so they are walked backward to face out again.
            const std::uint32_t source = mirrored ? count - 1 - corner : corner;
            out.indices.push_back(base + mesh.indices[cursor + source]);
        }
        cursor += count;
    }
}

std::string sanitizeName(const std::string& name)
{
    std::string clean;
    for (const char c : name)
        clean += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (clean.empty()) clean = "mesh";
    return clean;
}

// Gives the points of a mixed merge that came without normals the normal of their faces, so they
// are not published as zero vectors that the viewport would trust over a computation. The corners
// are already in their final, mirror corrected order, so the sums point out of the surface.
void fillMissingNormals(Gathered& data)
{
    if (!data.hasNormals) return;
    const bool anyMissing =
        std::find(data.normalMissing.begin(), data.normalMissing.end(), true) != data.normalMissing.end();
    if (!anyMissing) return;

    std::vector<Vector3> sums(data.positions.size(), Vector3::Zero());
    std::size_t cursor = 0;
    for (const Offset count : data.faceCounts)
    {
        // Newell's sum is the face normal scaled by twice the area, so larger faces weigh more.
        Vector3 face = Vector3::Zero();
        for (Offset corner = 0; corner < count; ++corner)
        {
            const Vector3& current = data.positions[data.indices[cursor + corner]];
            const Vector3& next = data.positions[data.indices[cursor + (corner + 1) % count]];
            face.x() += (current.y() - next.y()) * (current.z() + next.z());
            face.y() += (current.z() - next.z()) * (current.x() + next.x());
            face.z() += (current.x() - next.x()) * (current.y() + next.y());
        }
        for (Offset corner = 0; corner < count; ++corner)
            sums[data.indices[cursor + corner]] += face;
        cursor += count;
    }

    for (std::size_t point = 0; point < sums.size(); ++point)
    {
        if (!data.normalMissing[point]) continue;
        const float length = sums[point].norm();
        // A point no face uses, or whose faces cancel out, still gets a usable direction.
        data.normals[point] = length > 1e-20f ? Vector3(sums[point] / length) : Vector3(0.f, 1.f, 0.f);
    }
}

std::shared_ptr<geo::Mesh> toMesh(
    Gathered& data,
    const std::string& path,
    const std::vector<std::shared_ptr<const geo::Material>>& sceneMaterials
)
{
    fillMissingNormals(data);
    auto mesh = std::make_shared<geo::Mesh>(path);
    mesh->addPoints(data.positions);
    mesh->addFaces(data.indices, data.faceCounts, true);

    const Offset cornerCount = data.indices.size();
    if (data.hasNormals)
    {
        auto normals = mesh->addAttribute<Vector3>(attr::AttributeOwner::VERTEX, attr::names::normal);
        for (Offset corner = 0; corner < cornerCount; ++corner)
            normals.setValue(corner, data.normals[data.indices[corner]]);
    }
    if (data.hasUvs)
    {
        auto uvs = mesh->addAttribute<Vector3>(attr::AttributeOwner::VERTEX, attr::names::uv);
        for (Offset corner = 0; corner < cornerCount; ++corner)
            uvs.setValue(corner, data.uvs[data.indices[corner]]);
    }
    if (data.hasColors)
    {
        auto colors = mesh->addAttribute<Vector3>(attr::AttributeOwner::VERTEX, attr::names::color);
        for (Offset corner = 0; corner < cornerCount; ++corner)
            colors.setValue(corner, data.colors[data.indices[corner]]);
    }

    if (data.hasSkin)
    {
        for (int i = 0; i < attr::names::skinInfluenceCount; ++i)
        {
            auto joints = mesh->addAttribute<intT>(attr::AttributeOwner::POINT, attr::names::skinJoint[i]);
            auto weights = mesh->addAttribute<floatT>(attr::AttributeOwner::POINT, attr::names::skinWeight[i]);
            for (Offset point = 0; point < data.skinJoints.size(); ++point)
            {
                joints.setValue(point, data.skinJoints[point][i]);
                weights.setValue(point, data.skinWeights[point][i]);
            }
        }
    }

    // The mesh carries the materials its faces use, in the order they first appear, and the
    // index of a face points into that table.
    std::vector<intT> remap(sceneMaterials.size(), -1);
    std::vector<std::shared_ptr<const geo::Material>> table;
    auto materials = mesh->addAttribute<intT>(attr::AttributeOwner::FACE, attr::names::materialIndex);
    for (Offset face = 0; face < data.faceMaterials.size(); ++face)
    {
        intT index = data.faceMaterials[face];
        if (index >= 0 && static_cast<std::size_t>(index) < sceneMaterials.size())
        {
            if (remap[static_cast<std::size_t>(index)] < 0)
            {
                remap[static_cast<std::size_t>(index)] = static_cast<intT>(table.size());
                table.push_back(sceneMaterials[static_cast<std::size_t>(index)]);
            }
            index = remap[static_cast<std::size_t>(index)];
        }
        else
            index = -1;
        materials.setValue(face, index);
    }
    mesh->setMaterials(std::move(table));
    return mesh;
}

// The skeleton of a file as a mesh: a point per joint at its rest position, an open two point face
// per bone, and the transforms as point attributes. The correction of the up axis is applied to
// everything that lives in the space of the meshes, so the skeleton stands inside them.
std::shared_ptr<geo::Mesh> toSkeletonMesh(const RawSkeleton& raw, const Matrix4& correction)
{
    auto mesh = std::make_shared<geo::Mesh>("/skeleton");
    const std::size_t count = raw.joints.size();

    std::vector<Vector3> positions;
    positions.reserve(count);
    std::vector<Offset> bonePoints;
    std::vector<Offset> boneCounts;
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        const RawJoint& source = raw.joints[joint];
        positions.push_back(Vector3((correction * source.restWorld).col(3).head<3>()));
        if (source.parent >= 0)
        {
            bonePoints.push_back(static_cast<Offset>(source.parent));
            bonePoints.push_back(joint);
            boneCounts.push_back(2);
        }
    }
    mesh->addPoints(positions);
    if (!boneCounts.empty()) mesh->addFaces(bonePoints, boneCounts, false);

    auto parents = mesh->addAttribute<intT>(attr::AttributeOwner::POINT, attr::names::jointParent);
    auto rest = mesh->addAttribute<Matrix4>(attr::AttributeOwner::POINT, attr::names::restLocal);
    auto inverseBind = mesh->addAttribute<Matrix4>(attr::AttributeOwner::POINT, attr::names::inverseBind);
    auto pose = mesh->addAttribute<Matrix4>(attr::AttributeOwner::POINT, attr::names::pose);
    const Matrix4 uncorrect = correction.inverse();

    auto table = std::make_shared<geo::Skeleton>();
    table->jointNames.reserve(count);
    table->rootBase.reserve(count);
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        const RawJoint& source = raw.joints[joint];
        parents.setValue(joint, source.parent);
        rest.setValue(joint, source.restLocal);
        // The skinned vertices are in the corrected space, so what takes them to the joint is the
        // file's matrix after the correction is undone.
        inverseBind.setValue(joint, Matrix4(source.inverseBind * uncorrect));
        pose.setValue(joint, Matrix4(correction * source.restWorld));
        table->jointNames.push_back(source.name);
        table->rootBase.push_back(source.parent < 0 ? Matrix4(correction * source.parentBase) : Matrix4(Matrix4::Identity()));
    }
    table->clips = raw.clips;
    mesh->setSkeleton(std::move(table));
    return mesh;
}

} // namespace

ImportedFile buildMeshes(
    RawScene&& scene,
    const ImportOptions& options,
    UpAxis declaredUp,
    const std::string& readerName
)
{
    UpAxis up = options.upAxis;
    if (up == UpAxis::Auto) up = scene.declaredUp != UpAxis::Auto ? scene.declaredUp : declaredUp;
    const Matrix4 correction = up == UpAxis::Z ? zUpToYUp() : Matrix4::Identity();

    std::vector<Gathered> groups;
    if (options.mergeMeshes) groups.emplace_back();
    for (const RawInstance& instance : scene.instances)
    {
        if (instance.mesh >= scene.meshes.size()) continue;
        const RawMesh& mesh = scene.meshes[instance.mesh];
        if (!options.mergeMeshes) groups.emplace_back();
        if (groups.back().name.empty()) groups.back().name = mesh.name;
        // A skinned mesh is placed by its skeleton, not by the node that holds it: the inverse
        // bind matrices already carry the way from the mesh to the joints (glTF says so, and Assimp
        // folds the mesh transform into the offset matrices of the formats that have one).
        const bool skinned = !mesh.skinJoints.empty();
        gather(groups.back(), mesh, skinned ? correction : correction * instance.world);
    }

    ImportedFile result;
    ImportSummary& summary = result.summary;
    summary.reader = readerName;
    summary.animationCount = scene.animationCount;
    summary.skinnedMeshCount = scene.skinnedMeshCount;
    summary.skippedPrimitiveCount = scene.skippedPrimitiveCount;

    // The materials of the file, built once: every mesh that uses one shares it.
    std::vector<std::shared_ptr<const geo::Material>> sceneMaterials;
    for (const RawMaterial& raw : scene.materials)
    {
        auto material = std::make_shared<geo::Material>();
        material->name = raw.name;
        material->baseColor = Vector4(raw.baseColor[0], raw.baseColor[1], raw.baseColor[2], raw.baseColor[3]);
        material->baseColorTexture = raw.texture;
        material->missingTexture = raw.missingTexture;
        material->missingTextureName = raw.missingTextureName;
        material->missingTextureNotFound = raw.missingTextureNotFound;
        sceneMaterials.push_back(std::move(material));
    }

    // The mesh of a single file is named like the node of the obj reader, so a scene that holds
    // one mesh keeps the path an obj import has always had.
    std::set<std::string> usedPaths;
    for (Gathered& group : groups)
    {
        if (group.faceCounts.empty()) continue;
        std::string path = result.meshes.empty() && groups.size() == 1
                               ? "/mesh"
                               : "/" + sanitizeName(group.name);
        const std::string stem = path;
        for (int suffix = 2; !usedPaths.insert(path).second; ++suffix)
            path = stem + std::to_string(suffix);

        const std::shared_ptr<geo::Mesh> mesh = toMesh(group, path, sceneMaterials);
        summary.pointCount += mesh->getNumPoints();
        summary.faceCount += mesh->getNumFaces();
        summary.hasNormals = summary.hasNormals || group.hasNormals;
        summary.hasUvs = summary.hasUvs || group.hasUvs;
        summary.hasColors = summary.hasColors || group.hasColors;
        result.meshes.push_back(mesh);
    }
    summary.meshCount = result.meshes.size();

    // The skeleton only matters when a mesh that was kept is skinned to it.
    const bool anySkin = std::any_of(groups.begin(), groups.end(), [](const Gathered& group) {
        return group.hasSkin && !group.faceCounts.empty();
    });
    if (anySkin && !scene.skeleton.joints.empty())
    {
        result.skeleton = toSkeletonMesh(scene.skeleton, correction);
        summary.jointCount = scene.skeleton.joints.size();
        for (const RawJoint& joint : scene.skeleton.joints)
            if (joint.parent < 0) ++summary.rootJointCount;
        for (const auto& clip : scene.skeleton.clips)
            summary.clips.emplace_back(clip->name, clip->duration);
        summary.overInfluencedVertices = scene.skeleton.overInfluencedVertices;
        summary.cubicTracks = scene.skeleton.cubicTracks;
    }

    summary.materialCount = scene.materials.size();
    std::set<const geo::Image*> distinctImages;
    for (const RawMaterial& material : scene.materials)
    {
        if (material.hasTexture) ++summary.texturedMaterialCount;
        if (material.texture && distinctImages.insert(material.texture.get()).second)
        {
            ++summary.decodedTextureCount;
            summary.textureBytes += material.texture->byteSize();
            if (material.texture->source.find("(scaled down") != std::string::npos)
                ++summary.scaledTextureCount;
        }
        if (!material.missingTexture.empty())
            summary.missingTextures.push_back((material.name.empty() ? "material" : material.name) + ": " + material.missingTexture);
    }
    if (!scene.materials.empty())
    {
        summary.hasBaseColor = true;
        summary.baseColor = scene.materials.front().baseColor;
    }

    if (result.meshes.empty()) throw ImportError("The file holds no surface to import.");
    return result;
}

} // namespace helix::import
