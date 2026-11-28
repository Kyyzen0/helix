// The time half of the facade: the playback range and rate, and the keyframes of the float
// parameters. The frame itself (timeInfo, setFrame) is in HelixCore.cpp. See the Time and
// Keyframes sections of HelixCore.h.

#include "Engine/Expression/ExpressionEngine.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

// A frame further than this from zero is a typo, not a shot. It also keeps the integer frame
// range of the engine clear of overflow.
constexpr float maxFrameMagnitude = 1'000'000.f;
constexpr float maxFps = 1000.f;

sim::KeyframeStore& store() { return sim::KeyframeStore::instance(); }

sim::Interpolation toEngine(Interpolation interpolation)
{
    switch (interpolation)
    {
    case Interpolation::Step:
        return sim::Interpolation::Step;
    case Interpolation::Linear:
        return sim::Interpolation::Linear;
    case Interpolation::Smooth:
        return sim::Interpolation::Smooth;
    }
    return sim::Interpolation::Smooth;
}

Interpolation fromEngine(sim::Interpolation interpolation)
{
    switch (interpolation)
    {
    case sim::Interpolation::Step:
        return Interpolation::Step;
    case sim::Interpolation::Linear:
        return Interpolation::Linear;
    case sim::Interpolation::Smooth:
        return Interpolation::Smooth;
    }
    return Interpolation::Smooth;
}

// Whether the text of an expression reads the time: `frame()` or `time()` called by name, once
// `$F`, `$FF` and `$T` are expanded into them. A name inside a string is text, and `myframe(` or
// `a.frame(` is some other function, so only a whole word outside a string counts.
bool expressionReadsTime(const std::string& expression)
{
    const std::string text = expr::ExpressionEngine::expandVariables(expression);
    auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    bool inString = false;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const char c = text[index];
        if (inString)
        {
            if (c == '\\') ++index;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (!isWord(c) || (index > 0 && (isWord(text[index - 1]) || text[index - 1] == '.'))) continue;
        std::size_t end = index;
        while (end < text.size() && isWord(text[end])) ++end;
        const std::string word = text.substr(index, end - index);
        index = end - 1;
        if (word != "frame" && word != "time") continue;
        std::size_t after = end;
        while (after < text.size() && text[after] == ' ') ++after;
        if (after < text.size() && text[after] == '(') return true;
    }
    return false;
}

// Finds the float parameter a key would sit on, or says why there is none.
Status findKeyable(
    NodeId node,
    const std::string& name,
    std::uint32_t component,
    std::shared_ptr<prm::NodeParameter>& out
)
{
    if (Status status = detail::findNodeParameter(node, name, out); !status.ok) return status;
    if (out->getType() != prm::Type::FLOAT)
        return failure("The parameter '" + name + "' is not a float, and only a float can hold keyframes.");
    if (component >= out->getVectorSize())
        return failure(
            "Component " + std::to_string(component) + " is out of range, the parameter holds " +
            std::to_string(out->getVectorSize()) + "."
        );
    return success();
}

Status checkFrame(float frame)
{
    if (!std::isfinite(frame) || std::abs(frame) > maxFrameMagnitude)
        return failure("A keyframe sits on a frame within a million of zero.");
    return success();
}

// What changed with a key: the parameter shows the value of its track at the current frame (the
// store applies it), and the node is dirtied outright. A simulation reads this parameter at the
// frames before the current one too, so a key on another frame changes what it produces even
// when the value at the current frame did not move.
void keysChanged(NodeId node)
{
    nt::nm().getNode(node).dirtyNode(true);
    detail::noteSceneModified();
}

// The interpolation a new key takes: the one of the key before it, so a track keeps its style,
// or smooth when there is none before.
sim::Interpolation interpolationForNewKey(const sim::KeyframeTarget& target, float frame)
{
    const sim::KeyframeTrack* track = store().getTrack(target);
    if (!track) return sim::Interpolation::Smooth;
    if (const sim::Keyframe* existing = track->findKeyframe(frame)) return existing->interpolation;
    sim::Interpolation before = sim::Interpolation::Smooth;
    for (const sim::Keyframe& key : track->getKeyframes())
    {
        if (key.frame >= frame) break;
        before = key.interpolation;
    }
    return before;
}

} // namespace

namespace detail {

bool isKeyed(NodeId node, const std::string& parameter, std::uint32_t component)
{
    return store().hasTrack({node, parameter, component});
}

Status keyAtCurrentFrame(NodeId node, const std::string& parameter, std::uint32_t component, float value)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        if (!std::isfinite(value)) return failure("A keyframe holds a number.");
        const sim::KeyframeTarget target{node, parameter, component};
        const float frame = nt::nm().getFrame();
        store().setKeyframe(
            target, frame, detail::holdFloatToRange(*found, component, value), interpolationForNewKey(target, frame)
        );
        keysChanged(node);
        return success();
    });
}

} // namespace detail

Status setFrameRange(std::int32_t start, std::int32_t end)
{
    return guarded([&] {
        if (!isInitialized()) return failure("The engine is not initialized.");
        if (start >= end) return failure("The range ends after it starts: the last frame is above the first.");
        if (std::abs(static_cast<float>(start)) > maxFrameMagnitude || std::abs(static_cast<float>(end)) > maxFrameMagnitude)
            return failure("A frame of the range sits within a million of zero.");
        // The end is moved first when the range goes up, so neither call has to push the other
        // end along and the playhead is clamped once to the final range.
        nt::NetworkManager& manager = nt::nm();
        if (start > manager.getEndFrame())
        {
            manager.setEndFrame(end);
            manager.setStartFrame(start);
        }
        else
        {
            manager.setStartFrame(start);
            manager.setEndFrame(end);
        }
        detail::noteSceneModified();
        return success();
    });
}

Status setFps(float fps)
{
    return guarded([&] {
        if (!isInitialized()) return failure("The engine is not initialized.");
        if (!std::isfinite(fps) || fps <= 0.f || fps > maxFps)
            return failure("The frame rate is above 0 and at most 1000 frames a second.");
        nt::nm().setFps(fps);
        detail::noteSceneModified();
        return success();
    });
}

Status setKeyframe(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame,
    float value,
    Interpolation interpolation
)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        if (Status status = checkFrame(frame); !status.ok) return status;
        if (!std::isfinite(value)) return failure("A keyframe holds a number.");
        store().setKeyframe(
            {node, parameter, component}, frame, detail::holdFloatToRange(*found, component, value), toEngine(interpolation)
        );
        keysChanged(node);
        return success();
    });
}

Status setKeyframeAtCurrentValue(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame
)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        if (Status status = checkFrame(frame); !status.ok) return status;
        const sim::KeyframeTarget target{node, parameter, component};
        // The value the component shows now, an expression's result included.
        const float value = found->evalFloat(component);
        if (!std::isfinite(value)) return failure("The value is not a number, so it cannot be keyed.");
        store().setKeyframe(target, frame, detail::holdFloatToRange(*found, component, value), interpolationForNewKey(target, frame));
        keysChanged(node);
        return success();
    });
}

Status removeKeyframe(NodeId node, const std::string& parameter, std::uint32_t component, float frame)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        if (!store().removeKeyframe({node, parameter, component}, frame))
            return failure("There is no keyframe on that frame.");
        keysChanged(node);
        return success();
    });
}

Status moveKeyframe(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float fromFrame,
    float toFrame
)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        if (Status status = checkFrame(toFrame); !status.ok) return status;
        const sim::KeyframeTarget target{node, parameter, component};
        const sim::KeyframeTrack* track = store().getTrack(target);
        const sim::Keyframe* key = track ? track->findKeyframe(fromFrame) : nullptr;
        if (!key) return failure("There is no keyframe on that frame.");
        if (fromFrame == toFrame) return success();
        if (track->hasKeyframe(toFrame)) return failure("There is a keyframe on that frame already.");

        // The key is copied before the track changes under it. The new key goes in first, so the
        // track is never empty and the base value and expression it remembers are not let go.
        const sim::Keyframe moved = *key;
        store().setKeyframe(target, toFrame, moved.value, moved.interpolation);
        store().removeKeyframe(target, fromFrame);
        keysChanged(node);
        return success();
    });
}

Status setKeyframeInterpolation(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame,
    Interpolation interpolation
)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        const sim::KeyframeTarget target{node, parameter, component};
        const sim::KeyframeTrack* track = store().getTrack(target);
        const sim::Keyframe* key = track ? track->findKeyframe(frame) : nullptr;
        if (!key) return failure("There is no keyframe on that frame.");
        const float value = key->value;
        store().setKeyframe(target, frame, value, toEngine(interpolation));
        keysChanged(node);
        return success();
    });
}

Status clearKeyframes(NodeId node, const std::string& parameter, std::uint32_t component)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findKeyable(node, parameter, component, found); !status.ok) return status;
        const sim::KeyframeTarget target{node, parameter, component};
        if (!store().hasTrack(target)) return success();
        store().clearTrack(target);
        keysChanged(node);
        return success();
    });
}

std::vector<KeyframeTrackInfo> listKeyframes(NodeId node)
{
    std::vector<KeyframeTrackInfo> tracks;
    if (!detail::requireNodeInScene(node).ok) return tracks;
    for (const sim::KeyframeTarget& target : store().getTargets(node))
    {
        const sim::KeyframeTrack* track = store().getTrack(target);
        if (!track) continue;
        KeyframeTrackInfo info;
        info.parameter = target.parameter;
        info.component = target.component;
        info.baseValue = track->getBaseValue();
        for (const sim::Keyframe& key : track->getKeyframes())
            info.keys.push_back({key.frame, key.value, fromEngine(key.interpolation)});
        tracks.push_back(std::move(info));
    }
    return tracks;
}

std::vector<NodeId> animatedNodes()
{
    std::set<NodeId> nodes;
    if (!isInitialized()) return {};
    for (const sim::KeyframeTarget& target : store().getTargets())
        if (nt::nm().isValidNode(target.nodeId)) nodes.insert(target.nodeId);
    return {nodes.begin(), nodes.end()};
}

AnimationHints sceneAnimation()
{
    AnimationHints hints;
    if (!isInitialized()) return hints;
    hints.keyedNodes = static_cast<std::uint32_t>(animatedNodes().size());
    for (auto [id, node] : nt::nm().nodes())
    {
        const std::string type = node.getType().getFullName();
        if (type == "helix::skeletonPose") ++hints.skeletonPoseNodes;
        if (type == "helix::simulation") ++hints.simulationNodes;
        for (const std::weak_ptr<prm::NodeParameter>& weak : node.getParameters())
        {
            const std::shared_ptr<prm::NodeParameter> parameter = weak.lock();
            if (!parameter) continue;
            for (std::uint32_t component = 0; component < parameter->getVectorSize(); ++component)
            {
                if (!parameter->hasExpression(component)) continue;
                if (expressionReadsTime(parameter->getExpression(component).value_or("")))
                    ++hints.timeExpressions;
            }
        }
    }
    return hints;
}

} // namespace helix::core
