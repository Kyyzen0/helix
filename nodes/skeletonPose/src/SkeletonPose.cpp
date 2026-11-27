#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Primitives/Skeleton.h"
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace helix;

enum class LoopMode
{
    Loop,
    Hold,
    Once
};

LoopMode loopModeFrom(const std::string& value)
{
    if (value == "hold") return LoopMode::Hold;
    if (value == "once") return LoopMode::Once;
    return LoopMode::Loop;
}

class SkeletonPose : public nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;

  private:
    // Poses one skeleton mesh. The clip is null for the rest pose.
    void pose(geo::Mesh& skeleton, const geo::AnimationClip* clip, float clipTime, float lengthScale);
    // The clip a skeleton plays, or null (with a warning or an error recorded) when there is none.
    const geo::AnimationClip* chooseClip(const geo::Skeleton* table, bool& failed);
};

bool isSkeleton(const geo::Primitive& primitive)
{
    const auto* mesh = dynamic_cast<const geo::Mesh*>(&primitive);
    return mesh && std::as_const(*mesh).getAttribByName(attr::AttributeOwner::POINT, attr::names::jointParent) &&
           std::as_const(*mesh).getAttribByName(attr::AttributeOwner::POINT, attr::names::restLocal);
}

// The time inside the clip a time on the clock falls on, for the loop mode. `rest` is set when the
// skeleton should stand in its rest pose instead.
float clipTimeFor(double time, float duration, LoopMode mode, bool& rest)
{
    rest = false;
    if (!(duration > 0.f))
    {
        // A clip with no length is one pose: it plays at its first key, and "once" is over at once.
        rest = mode == LoopMode::Once && time >= 0.0;
        return 0.f;
    }
    switch (mode)
    {
    case LoopMode::Loop:
    {
        const double wrapped = time - std::floor(time / duration) * duration;
        return static_cast<float>(std::clamp(wrapped, 0.0, double(duration)));
    }
    case LoopMode::Hold:
        return static_cast<float>(std::clamp(time, 0.0, double(duration)));
    case LoopMode::Once:
        rest = time < 0.0 || time >= duration;
        return static_cast<float>(std::clamp(time, 0.0, double(duration)));
    }
    return 0.f;
}

std::string clipList(const geo::Skeleton& table)
{
    std::string text;
    std::size_t shown = 0;
    for (const auto& clip : table.clips)
    {
        if (shown == 8)
        {
            text += ", and " + std::to_string(table.clips.size() - shown) + " more";
            break;
        }
        if (shown > 0) text += ", ";
        // A name from a file can be any length, and an error is read in a narrow column.
        text += "'" + clip->name.substr(0, 60) + "'";
        ++shown;
    }
    return text;
}

const geo::AnimationClip* SkeletonPose::chooseClip(const geo::Skeleton* table, bool& failed)
{
    failed = false;
    if (!table || table->clips.empty())
    {
        throwWarning("The skeleton has no clip, so it stays in its rest pose.");
        return nullptr;
    }
    std::string wanted = evalParmString("clip");
    boost::trim(wanted);
    if (wanted.empty()) return table->clips.front().get();
    const geo::AnimationClip* clip = table->findClip(wanted);
    if (!clip)
    {
        throwError(
            "The skeleton has no clip named '" + wanted.substr(0, 60) + "'. It has: " + clipList(*table) + "."
        );
        failed = true;
    }
    return clip;
}

void SkeletonPose::pose(geo::Mesh& skeleton, const geo::AnimationClip* clip, float clipTime, float lengthScale)
{
    const std::size_t count = skeleton.getNumPoints();
    const auto parents = attr::AttributeHandleRO<intT>(
        std::as_const(skeleton).getAttribByName(attr::AttributeOwner::POINT, attr::names::jointParent)
    );
    const auto rest = attr::AttributeHandleRO<Matrix4>(
        std::as_const(skeleton).getAttribByName(attr::AttributeOwner::POINT, attr::names::restLocal)
    );
    const geo::Skeleton* table = skeleton.skeleton().get();

    for (std::size_t joint = 0; joint < count; ++joint)
    {
        const intT parent = parents.getValue(joint);
        if (parent < -1 || parent >= static_cast<intT>(count) || parent == static_cast<intT>(joint))
        {
            throwError("Joint " + std::to_string(joint) + " has a parent index that is not a joint of the skeleton.");
            return;
        }
    }

    // The local transform of each joint at the clip time: what the clip says, and the rest value
    // for what it does not.
    std::vector<Matrix4> local(count);
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        local[joint] = rest.getValue(joint);
        if (!clip || joint >= clip->joints.size() || !clip->joints[joint].any()) continue;
        const geo::JointTracks& tracks = clip->joints[joint];
        Vector3 translation;
        geo::Quaternion rotation;
        Vector3 scale;
        geo::decomposeTrs(local[joint], translation, rotation, scale);
        // The keys are in the units of the file, and the import's size has scaled the skeleton.
        if (!tracks.translation.empty()) translation = geo::sampleTrack(tracks.translation, clipTime) * lengthScale;
        if (!tracks.rotation.empty()) rotation = geo::sampleTrack(tracks.rotation, clipTime);
        if (!tracks.scale.empty()) scale = geo::sampleTrack(tracks.scale, clipTime);
        local[joint] = geo::composeTrs(translation, rotation, scale);
    }

    // World transforms down the hierarchy. A parent is not required to come before its child, so
    // each joint walks up to a joint that is done (or a root), and a walk that never ends is a
    // loop in the parent indices.
    std::vector<Matrix4> world(count, Matrix4::Identity());
    std::vector<char> done(count, 0);
    std::vector<std::size_t> chain;
    for (std::size_t start = 0; start < count; ++start)
    {
        chain.clear();
        for (std::size_t joint = start; !done[joint];)
        {
            chain.push_back(joint);
            if (chain.size() > count)
            {
                throwError("The parents of the joints form a loop, so the skeleton has no root to pose from.");
                return;
            }
            const intT parent = parents.getValue(joint);
            if (parent < 0) break;
            joint = static_cast<std::size_t>(parent);
        }
        for (auto joint = chain.rbegin(); joint != chain.rend(); ++joint)
        {
            const intT parent = parents.getValue(*joint);
            Matrix4 above = Matrix4::Identity();
            if (parent >= 0)
                above = world[static_cast<std::size_t>(parent)];
            else if (table && *joint < table->rootBase.size())
            {
                above = table->rootBase[*joint];
                above.col(3).head<3>() *= lengthScale;
            }
            world[*joint] = above * local[*joint];
            done[*joint] = 1;
        }
    }

    auto poses = skeleton.addAttribute<Matrix4>(attr::AttributeOwner::POINT, attr::names::pose);
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        poses.setValue(joint, world[joint]);
        skeleton.setPointPos(joint, world[joint].col(3).head<3>());
    }
}

void SkeletonPose::cook()
{
    if (!outputRequested(0)) return;

    NodePacket packet = cloneInputPacket(0);
    if (!hasInput(0))
    {
        throwError("Connect a skeleton to the input: the second output of a Geometry Import node reading a rigged file.");
        return;
    }

    // Which meshes of the input are skeletons. An input that holds geometry and no skeleton is a
    // mistake worth saying; an empty mesh is what an import of a file with no rig gives, and passes
    // through with a note.
    std::vector<geo::Mesh*> skeletons;
    bool anyPoints = false;
    for (const auto& primitive : packet.getPrimitives())
    {
        if (primitive && primitive->getNumPoints() > 0) anyPoints = true;
        if (primitive && isSkeleton(*primitive)) skeletons.push_back(static_cast<geo::Mesh*>(primitive.get()));
    }
    if (skeletons.empty())
    {
        if (anyPoints)
        {
            throwError(
                "The input is not a skeleton: it has no joint_parent and rest_local point attributes. "
                "Use the second output of a Geometry Import node that reads a rigged file."
            );
            return;
        }
        throwWarning("The input holds no skeleton: the file has no rig, or no file is set.");
        setOutputPacket(0, std::move(packet));
        return;
    }

    for (geo::Mesh* skeleton : skeletons)
    {
        bool failed = false;
        const geo::AnimationClip* clip = chooseClip(skeleton->skeleton().get(), failed);
        if (failed) return;

        float clipTime = 0.f;
        if (clip)
        {
            // The scene time is the only thing the pose depends on, so the same frame always gives
            // the same pose, however it was reached.
            const double time = double(evalParmFloat("offset")) + double(getTime()) * double(evalParmFloat("speed"));
            bool atRest = false;
            clipTime = clipTimeFor(time, clip->duration, loopModeFrom(evalParmString("loop")), atRest);
            if (atRest) clip = nullptr;
        }
        pose(*skeleton, clip, clipTime, skeleton->skeleton() ? skeleton->skeleton()->scale : 1.f);
    }
    setOutputPacket(0, std::move(packet));
}

} // namespace

HELIX_REGISTER_NODE(skeletonPose, SkeletonPose)
