#include "Engine/Import/AssimpSkeleton.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace helix::import {

namespace {

constexpr int kInfluences = 4;

// Assimp reads the ticks per second of a file without one as 0, and the convention it documents
// for that is 25.
constexpr double kDefaultTicksPerSecond = 25.0;

// Walks the hierarchy once, giving each node its transform in the space of the file and the
// first node of each name (a bone names its node, and the first one wins when names repeat).
struct NodeIndex
{
    std::unordered_map<const aiNode*, Matrix4> world;
    std::unordered_map<std::string, const aiNode*> byName;

    explicit NodeIndex(const aiNode* root)
    {
        struct Pending
        {
            const aiNode* node;
            Matrix4 parent;
        };
        std::vector<Pending> pending{{root, Matrix4::Identity()}};
        while (!pending.empty())
        {
            const Pending current = pending.back();
            pending.pop_back();
            const Matrix4 here = current.parent * assimpMatrix(current.node->mTransformation);
            world.emplace(current.node, here);
            byName.emplace(current.node->mName.C_Str(), current.node);
            for (unsigned i = current.node->mNumChildren; i-- > 0;)
                pending.push_back({current.node->mChildren[i], here});
        }
    }
};

// The influences of one vertex while the bones are read: the four strongest seen so far.
struct Influences
{
    std::array<std::uint32_t, kInfluences> joint{};
    std::array<float, kInfluences> weight{};
    unsigned count = 0;
    // Every influence offered, so a vertex pulled by more than four joints can be told.
    unsigned seen = 0;
};

void offer(Influences& vertex, std::uint32_t joint, float weight)
{
    ++vertex.seen;
    if (vertex.count < kInfluences)
    {
        vertex.joint[vertex.count] = joint;
        vertex.weight[vertex.count] = weight;
        ++vertex.count;
        return;
    }
    // Full: the new influence replaces the weakest one when it is stronger.
    unsigned weakest = 0;
    for (unsigned i = 1; i < kInfluences; ++i)
        if (vertex.weight[i] < vertex.weight[weakest]) weakest = i;
    if (weight > vertex.weight[weakest])
    {
        vertex.joint[weakest] = joint;
        vertex.weight[weakest] = weight;
    }
}

// What a track of keys says: the interpolation of the first key, and for a cubic spline (which
// Assimp stores as an in tangent, a value and an out tangent per key) the values alone.
template <typename Key> struct KeyReading
{
    geo::Interpolation interpolation = geo::Interpolation::Linear;
    bool cubic = false;
    // The index of the value of each logical key in the array of keys, which is every key for a
    // plain track and the middle one of each triple for a cubic one.
    std::vector<unsigned> valueKeys;
};

template <typename Key> KeyReading<Key> readKeys(const Key* keys, unsigned count)
{
    KeyReading<Key> reading;
    if (count == 0) return reading;
    switch (keys[0].mInterpolation)
    {
    case aiAnimInterpolation_Step:
        reading.interpolation = geo::Interpolation::Step;
        break;
    case aiAnimInterpolation_Cubic_Spline:
        reading.cubic = true;
        break;
    default:
        break;
    }
    if (reading.cubic)
    {
        // A cubic track whose key count is not a multiple of three is not one Assimp wrote.
        if (count % 3 != 0) return reading;
        for (unsigned key = 1; key < count; key += 3)
            reading.valueKeys.push_back(key);
    }
    else
        for (unsigned key = 0; key < count; ++key)
            reading.valueKeys.push_back(key);
    return reading;
}

float seconds(double time, double ticksPerSecond) { return static_cast<float>(time / ticksPerSecond); }

} // namespace

SkeletonReader::SkeletonReader(const aiScene& scene) : scene_(scene)
{
    if (!scene.mRootNode) return;

    // Every bone of every mesh, by name, with the first offset matrix seen for it.
    std::vector<std::string> boneNames;
    std::unordered_map<std::string, aiMatrix4x4> offsetOf;
    for (unsigned m = 0; m < scene.mNumMeshes; ++m)
    {
        const aiMesh& mesh = *scene.mMeshes[m];
        for (unsigned b = 0; b < mesh.mNumBones; ++b)
        {
            const std::string name = mesh.mBones[b]->mName.C_Str();
            if (offsetOf.emplace(name, mesh.mBones[b]->mOffsetMatrix).second) boneNames.push_back(name);
        }
    }
    if (boneNames.empty()) return;

    const NodeIndex nodes(scene.mRootNode);

    // The nodes the bones name, then the nodes between two of them that are not bones themselves
    // (a joint hierarchy that goes through a plain node would otherwise be cut in two).
    std::unordered_set<const aiNode*> boneNodes;
    std::vector<std::string> orphans;
    for (const std::string& name : boneNames)
    {
        const auto found = nodes.byName.find(name);
        if (found == nodes.byName.end())
            orphans.push_back(name);
        else
            boneNodes.insert(found->second);
    }
    std::unordered_set<const aiNode*> jointNodes = boneNodes;
    for (const aiNode* node : boneNodes)
    {
        std::vector<const aiNode*> between;
        for (const aiNode* above = node->mParent; above; above = above->mParent)
        {
            if (boneNodes.count(above))
            {
                jointNodes.insert(between.begin(), between.end());
                break;
            }
            between.push_back(above);
        }
    }

    // Depth first from the root, so a parent comes before its children and the order does not
    // depend on how the file lists its bones.
    struct Pending
    {
        const aiNode* node;
        int parentJoint;
    };
    std::vector<Pending> pending{{scene.mRootNode, -1}};
    while (!pending.empty())
    {
        const Pending current = pending.back();
        pending.pop_back();
        int parentForChildren = current.parentJoint;
        if (jointNodes.count(current.node))
        {
            RawJoint joint;
            joint.name = current.node->mName.C_Str();
            joint.parent = current.parentJoint;
            joint.restLocal = assimpMatrix(current.node->mTransformation);
            joint.restWorld = nodes.world.at(current.node);
            if (joint.parent < 0 && current.node->mParent)
                joint.parentBase = nodes.world.at(current.node->mParent);
            const auto offset = offsetOf.find(joint.name);
            joint.inverseBind = offset != offsetOf.end() ? assimpMatrix(offset->second) : Matrix4(joint.restWorld.inverse());
            parentForChildren = static_cast<int>(skeleton_.joints.size());
            jointOfName_.emplace(joint.name, skeleton_.joints.size());
            skeleton_.joints.push_back(std::move(joint));
        }
        for (unsigned i = current.node->mNumChildren; i-- > 0;)
            pending.push_back({current.node->mChildren[i], parentForChildren});
    }

    // A bone that names no node has nowhere in the hierarchy: it stands alone at the origin and
    // holds the mesh at its bind pose.
    for (const std::string& name : orphans)
    {
        RawJoint joint;
        joint.name = name;
        joint.inverseBind = assimpMatrix(offsetOf.at(name));
        joint.restWorld = joint.inverseBind.inverse();
        joint.restLocal = joint.restWorld;
        jointOfName_.emplace(name, skeleton_.joints.size());
        skeleton_.joints.push_back(std::move(joint));
    }
}

void SkeletonReader::readSkin(const aiMesh& source, RawMesh& mesh)
{
    if (!hasSkeleton() || source.mNumBones == 0) return;

    std::vector<Influences> vertices(source.mNumVertices);
    for (unsigned b = 0; b < source.mNumBones; ++b)
    {
        const aiBone& bone = *source.mBones[b];
        const auto joint = jointOfName_.find(bone.mName.C_Str());
        if (joint == jointOfName_.end()) continue;
        for (unsigned w = 0; w < bone.mNumWeights; ++w)
        {
            const aiVertexWeight& influence = bone.mWeights[w];
            // A joint the file lists with no influence carries a dummy weight of 0.
            if (influence.mVertexId >= source.mNumVertices || !(influence.mWeight > 0.f) ||
                !std::isfinite(influence.mWeight))
                continue;
            offer(vertices[influence.mVertexId], static_cast<std::uint32_t>(joint->second), influence.mWeight);
        }
    }

    mesh.skinJoints.assign(std::size_t(kInfluences) * source.mNumVertices, 0u);
    mesh.skinWeights.assign(std::size_t(kInfluences) * source.mNumVertices, 0.f);
    for (unsigned v = 0; v < source.mNumVertices; ++v)
    {
        Influences& vertex = vertices[v];
        if (vertex.seen > kInfluences) ++skeleton_.overInfluencedVertices;
        // The strongest first, so influence 0 is the one that matters most.
        std::array<unsigned, kInfluences> order{0, 1, 2, 3};
        std::stable_sort(order.begin(), order.begin() + vertex.count, [&](unsigned a, unsigned b) {
            return vertex.weight[a] > vertex.weight[b];
        });
        float total = 0.f;
        for (unsigned i = 0; i < vertex.count; ++i)
            total += vertex.weight[order[i]];
        if (!(total > 0.f)) continue;
        for (unsigned i = 0; i < vertex.count; ++i)
        {
            mesh.skinJoints[std::size_t(kInfluences) * v + i] = vertex.joint[order[i]];
            mesh.skinWeights[std::size_t(kInfluences) * v + i] = vertex.weight[order[i]] / total;
        }
    }
}

void SkeletonReader::readClips()
{
    if (!hasSkeleton()) return;
    for (unsigned a = 0; a < scene_.mNumAnimations; ++a)
    {
        const aiAnimation& animation = *scene_.mAnimations[a];
        const double ticksPerSecond = animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : kDefaultTicksPerSecond;

        auto clip = std::make_shared<geo::AnimationClip>();
        clip->name = animation.mName.C_Str();
        if (clip->name.empty()) clip->name = "Clip " + std::to_string(a + 1);
        clip->joints.resize(skeleton_.joints.size());

        float duration = 0.f;
        for (unsigned c = 0; c < animation.mNumChannels; ++c)
        {
            const aiNodeAnim& channel = *animation.mChannels[c];
            const auto joint = jointOfName_.find(channel.mNodeName.C_Str());
            if (joint == jointOfName_.end())
            {
                ++skeleton_.unmatchedTracks;
                continue;
            }
            geo::JointTracks& tracks = clip->joints[joint->second];

            const auto translation = readKeys(channel.mPositionKeys, channel.mNumPositionKeys);
            if (translation.cubic && !translation.valueKeys.empty()) ++skeleton_.cubicTracks;
            tracks.translation.interpolation = translation.interpolation;
            for (const unsigned key : translation.valueKeys)
            {
                const aiVectorKey& k = channel.mPositionKeys[key];
                tracks.translation.times.push_back(seconds(k.mTime, ticksPerSecond));
                tracks.translation.values.emplace_back(k.mValue.x, k.mValue.y, k.mValue.z);
            }

            const auto rotation = readKeys(channel.mRotationKeys, channel.mNumRotationKeys);
            if (rotation.cubic && !rotation.valueKeys.empty()) ++skeleton_.cubicTracks;
            tracks.rotation.interpolation = rotation.interpolation;
            for (const unsigned key : rotation.valueKeys)
            {
                const aiQuatKey& k = channel.mRotationKeys[key];
                tracks.rotation.times.push_back(seconds(k.mTime, ticksPerSecond));
                tracks.rotation.values.emplace_back(k.mValue.w, k.mValue.x, k.mValue.y, k.mValue.z);
            }

            const auto scaling = readKeys(channel.mScalingKeys, channel.mNumScalingKeys);
            if (scaling.cubic && !scaling.valueKeys.empty()) ++skeleton_.cubicTracks;
            tracks.scale.interpolation = scaling.interpolation;
            for (const unsigned key : scaling.valueKeys)
            {
                const aiVectorKey& k = channel.mScalingKeys[key];
                tracks.scale.times.push_back(seconds(k.mTime, ticksPerSecond));
                tracks.scale.values.emplace_back(k.mValue.x, k.mValue.y, k.mValue.z);
            }

            for (const std::vector<float>* times :
                 {&tracks.translation.times, &tracks.rotation.times, &tracks.scale.times})
                if (!times->empty()) duration = std::max(duration, times->back());
        }
        // A clip with no key at all still has the length the file gives it.
        if (duration <= 0.f) duration = static_cast<float>(animation.mDuration / ticksPerSecond);
        clip->duration = std::max(duration, 0.f);
        skeleton_.clips.push_back(std::move(clip));
    }
}

} // namespace helix::import
