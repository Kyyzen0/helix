#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace helix;

// The rows of a skin matrix that move a point: the 3 x 3 block and the translation.
using Affine = Eigen::Matrix<float, 3, 4>;

constexpr int kInfluences = attr::names::skinInfluenceCount;

// A point is handed to a worker in runs this long, which keeps the cost of the hand-over small
// against the few dozen flops a point takes.
constexpr std::size_t kGrain = 2048;

class SkinDeform : public nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

// The skin attributes of a mesh, read only.
struct Skin
{
    std::vector<attr::AttributeHandleRO<intT>> joints;
    std::vector<attr::AttributeHandleRO<floatT>> weights;
};

// Nothing when the mesh lacks any of the eight attributes, or has one of the wrong type.
std::optional<Skin> readSkin(const geo::Mesh& mesh)
{
    Skin skin;
    for (int i = 0; i < kInfluences; ++i)
    {
        const auto joints = mesh.getAttribByName(attr::AttributeOwner::POINT, attr::names::skinJoint[i]);
        const auto weights = mesh.getAttribByName(attr::AttributeOwner::POINT, attr::names::skinWeight[i]);
        if (!joints || joints->getType() != attr::AttributeType::intT) return std::nullopt;
        if (!weights || weights->getType() != attr::AttributeType::floatT) return std::nullopt;
        skin.joints.emplace_back(joints);
        skin.weights.emplace_back(weights);
    }
    return skin;
}

// The first skeleton mesh of a packet that carries a pose and an inverse bind matrix per joint.
const geo::Mesh* findPosedSkeleton(const NodePacket& packet)
{
    for (const auto& primitive : packet.getPrimitives())
    {
        const auto* mesh = dynamic_cast<const geo::Mesh*>(primitive.get());
        if (!mesh) continue;
        const auto pose = mesh->getAttribByName(attr::AttributeOwner::POINT, attr::names::pose);
        const auto inverseBind = mesh->getAttribByName(attr::AttributeOwner::POINT, attr::names::inverseBind);
        if (pose && pose->getType() == attr::AttributeType::matrixT && inverseBind &&
            inverseBind->getType() == attr::AttributeType::matrixT)
            return mesh;
    }
    return nullptr;
}

// A Normal attribute of this owner, when the mesh has one that holds vectors.
std::shared_ptr<attr::Attribute> findNormals(geo::Mesh& mesh, attr::AttributeOwner owner)
{
    auto normals = mesh.getAttribByName(owner, attr::names::normal);
    if (normals && normals->getType() == attr::AttributeType::vectorT) return normals;
    return nullptr;
}

void SkinDeform::cook()
{
    if (!outputRequested(0)) return;

    if (!hasInput(1))
    {
        throwError("Connect the posed skeleton to the second input: the output of a Skeleton Pose node.");
        return;
    }
    NodePacket packet = cloneInputPacket(0);
    const std::shared_ptr<const NodePacket> skeletonPacket = getInputPacket(1);
    const geo::Mesh* skeleton = skeletonPacket ? findPosedSkeleton(*skeletonPacket) : nullptr;
    if (!skeleton)
    {
        throwError(
            "The second input is not a posed skeleton: it has no pose and inverse_bind point attributes. "
            "Connect the output of a Skeleton Pose node."
        );
        return;
    }

    // One skin matrix per joint: the pose of the joint after the matrix that takes the mesh to the
    // joint at rest. A point is moved by the weighted blend of the matrices of its joints.
    const std::size_t jointCount = skeleton->getNumPoints();
    const attr::AttributeHandleRO<Matrix4> poses(skeleton->getAttribByName(attr::AttributeOwner::POINT, attr::names::pose));
    const attr::AttributeHandleRO<Matrix4> inverseBinds(
        skeleton->getAttribByName(attr::AttributeOwner::POINT, attr::names::inverseBind)
    );
    if (poses.getSize() < jointCount || inverseBinds.getSize() < jointCount)
    {
        throwError("The skeleton's pose or inverse_bind attribute does not hold a matrix for every joint.");
        return;
    }
    std::vector<Affine> skinMatrices(jointCount);
    for (std::size_t joint = 0; joint < jointCount; ++joint)
    {
        const Matrix4 skin = poses[joint] * inverseBinds[joint];
        skinMatrices[joint] = skin.topRows<3>();
    }

    // The meshes to deform: every mesh that has a skin. A mesh without one is passed through, and
    // an input with no skinned mesh at all is an error, since the node would do nothing.
    std::vector<geo::Mesh*> skinned;
    std::size_t plain = 0;
    for (const auto& primitive : packet.getPrimitives())
    {
        auto* mesh = dynamic_cast<geo::Mesh*>(primitive.get());
        if (!mesh) continue;
        if (readSkin(*mesh))
            skinned.push_back(mesh);
        else
            ++plain;
    }
    if (skinned.empty())
    {
        throwError(
            "The mesh has no skin: it needs the point attributes skin_joint0 to skin_joint3 and "
            "skin_weight0 to skin_weight3. A rigged file gets them when Geometry Import reads it."
        );
        return;
    }
    if (plain > 0)
        throwWarning(std::to_string(plain) + (plain == 1 ? " mesh has" : " meshes have") + " no skin and pass through unmoved.");

    const bool deformNormals = evalParmBool("normals");
    for (geo::Mesh* mesh : skinned)
    {
        const std::optional<Skin> skin = readSkin(*mesh);
        const std::size_t pointCount = mesh->getNumPoints();

        // The joints a point names have to exist, or the pose would be read from nowhere. An
        // influence with no weight names nothing, so it is not held to this.
        for (std::size_t point = 0; point < pointCount; ++point)
        {
            if (!mesh->isValidPoint(point)) continue;
            for (int i = 0; i < kInfluences; ++i)
            {
                if (!(skin->weights[i][point] > 0.f)) continue;
                const intT joint = skin->joints[i][point];
                if (joint < 0 || joint >= static_cast<intT>(jointCount))
                {
                    throwError(
                        "Point " + std::to_string(point) + " is skinned to joint " + std::to_string(joint) +
                        ", but the skeleton has " + std::to_string(jointCount) + " joints."
                    );
                    return;
                }
            }
        }

        const std::shared_ptr<attr::Attribute> pointNormals =
            deformNormals ? findNormals(*mesh, attr::AttributeOwner::POINT) : nullptr;
        const std::shared_ptr<attr::Attribute> vertexNormals =
            deformNormals ? findNormals(*mesh, attr::AttributeOwner::VERTEX) : nullptr;
        // The blend of each point is kept when normals follow, since the same 3 x 3 block turns
        // them. A point with no weight is not moved, and neither is its normal.
        const bool keepBlends = pointNormals || vertexNormals;
        std::vector<Matrix3> blends(keepBlends ? pointCount : 0);
        std::vector<char> moved(keepBlends ? pointCount : 0, 0);

        // Gives the positions a store of their own before the workers read and write them. Each
        // point is read and then written by the one worker that owns it.
        if (pointCount > 0) mesh->setPointPos(0, mesh->getPointPos(0));
        const std::span<const Vector3> positions = mesh->pointPosSpan();
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, pointCount, kGrain),
            [&](const tbb::blocked_range<std::size_t>& range) {
                for (std::size_t point = range.begin(); point < range.end(); ++point)
                {
                    float total = 0.f;
                    for (int i = 0; i < kInfluences; ++i)
                        total += skin->weights[i][point] > 0.f ? skin->weights[i][point] : 0.f;
                    // Weights that do not add to 1 still blend as their shares of the whole.
                    if (!(total > 1e-8f)) continue;

                    Affine blend = Affine::Zero();
                    for (int i = 0; i < kInfluences; ++i)
                    {
                        const float weight = skin->weights[i][point];
                        if (!(weight > 0.f)) continue;
                        blend += (weight / total) * skinMatrices[static_cast<std::size_t>(skin->joints[i][point])];
                    }
                    const Vector3 position = positions[point];
                    mesh->setPointPos(point, Vector3(blend.leftCols<3>() * position + blend.col(3)));
                    if (keepBlends)
                    {
                        blends[point] = blend.leftCols<3>();
                        moved[point] = 1;
                    }
                }
            }
        );

        // A blend of rotations is not a rotation and shortens a unit normal, so each normal is
        // brought back to unit length. Rigid joints need no inverse transpose; a joint that
        // stretches unevenly gets normals that are slightly off, which skinning has always accepted.
        const auto turn = [](const Matrix3& blend, const Vector3& normal) {
            const Vector3 turned = blend * normal;
            const float length = turned.norm();
            return length > 1e-20f ? Vector3(turned / length) : normal;
        };
        if (pointNormals)
        {
            attr::AttributeHandle<Vector3> normals(pointNormals);
            const std::size_t count = std::min(pointCount, normals.getSize());
            tbb::parallel_for(tbb::blocked_range<std::size_t>(0, count, kGrain), [&](const tbb::blocked_range<std::size_t>& range) {
                for (std::size_t point = range.begin(); point < range.end(); ++point)
                    if (moved[point]) normals.setValue(point, turn(blends[point], normals[point]));
            });
        }
        if (vertexNormals)
        {
            attr::AttributeHandle<Vector3> normals(vertexNormals);
            const std::span<const intT> vertexPoints = mesh->vertexPointSpan();
            const std::size_t count = std::min(vertexPoints.size(), normals.getSize());
            tbb::parallel_for(tbb::blocked_range<std::size_t>(0, count, kGrain), [&](const tbb::blocked_range<std::size_t>& range) {
                for (std::size_t vertex = range.begin(); vertex < range.end(); ++vertex)
                {
                    const intT point = vertexPoints[vertex];
                    if (point >= 0 && static_cast<std::size_t>(point) < pointCount && moved[static_cast<std::size_t>(point)])
                        normals.setValue(vertex, turn(blends[static_cast<std::size_t>(point)], normals[vertex]));
                }
            });
        }
    }
    setOutputPacket(0, std::move(packet));
}

} // namespace

HELIX_REGISTER_NODE(skinDeform, SkinDeform)
