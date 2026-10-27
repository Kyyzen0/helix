#include "Engine/Simulation/KeyframeStore.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/NetworkGraph/NetworkGraph.h"
#include "Engine/NetworkGraph/Unit.h"
#include "Engine/Parameter/NodeParameter.h"
#include <algorithm>
#include <cmath>
#include <variant>

namespace helix::sim {

namespace {

// The two values are treated as the same when they are this close, so a value
// that only moved in its last bit does not dirty the scene on every frame.
constexpr floatT kValueEpsilon = 1e-6f;

// Returns the parameter a target names, or null when the node or the parameter
// is not there.
std::shared_ptr<prm::NodeParameter> findParameter(const KeyframeTarget& target)
{
    if (!nt::nm().isValidNode(target.nodeId)) return nullptr;
    return nt::nm().getNode(target.nodeId).getParameter(target.parameter).lock();
}

// Tells the engine that a value no longer reads the scene time, so a scene
// with no keyframes left stops being moved by the clock.
void clearTimeDependency(const KeyframeTarget& target)
{
    nt::nm().graph().setTimeDependent(nt::Unit{target.nodeId, target.parameter, target.component}, false);
}

} // namespace

KeyframeStore& KeyframeStore::instance()
{
    static KeyframeStore store;
    return store;
}

KeyframeStore::KeyframeStore()
{
    // The scene moving to a new frame resolves every track and writes the
    // results into the parameters they belong to. The engine raises this
    // signal before it dirties the nodes that read the time, so a node that
    // cooks because of a keyframe reads the value of the frame it sits on.
    frameSubscription_ = nt::nm().frameChanged.connect([this](floatT frame) {
        applyToParameters(frame);
    });

    // A node that goes away takes its keyframes with it.
    nodeRemovedSubscription_ = nt::nm().nodeRemoved.connect([this](nt::NodeId nodeId) {
        removeNode(nodeId);
    });

    // Clearing the scene clears the keyframes too.
    networkClearedSubscription_ = nt::nm().networkCleared.connect([this]() { clear(); });
}

void KeyframeStore::setKeyframe(
    const KeyframeTarget& target,
    floatT frame,
    floatT value,
    Interpolation interpolation
)
{
    auto& track = tracks_[target];

    // The first keyframe of a value captures what the parameter held, so the
    // parameter can be put back when the keyframes go away.
    if (track.isEmpty())
    {
        if (auto parameter = findParameter(target))
        {
            track.setBaseValue(parameter->evalFloat(target.component));
            baseExpressions_[target] = parameter->getExpression(target.component);
        }
    }

    track.addKeyframe(frame, value, interpolation);

    // The value now depends on the frame, so the engine is told to dirty it
    // whenever the scene moves.
    nt::nm().graph().setTimeDependent(nt::Unit{target.nodeId, target.parameter, target.component}, true);

    trackChanged(target);
    tracksChanged();

    // The value the parameter shows has to follow the frame straight away, so
    // a keyframe made on a frame the scene is not sitting on still shows its
    // own value.
    applyToParameters(nt::nm().getFrame());
}

bool KeyframeStore::removeKeyframe(const KeyframeTarget& target, floatT frame)
{
    auto found = tracks_.find(target);
    if (found == tracks_.end()) return false;
    if (!found->second.removeKeyframe(frame)) return false;

    if (found->second.isEmpty()) clearTrack(target);
    else
    {
        trackChanged(target);
        tracksChanged();
        applyToParameters(nt::nm().getFrame());
    }
    return true;
}

void KeyframeStore::clearTrack(const KeyframeTarget& target)
{
    auto found = tracks_.find(target);
    if (found == tracks_.end()) return;

    // The parameter goes back to the value it held before the track was made.
    if (auto parameter = findParameter(target))
    {
        parameter->setFloat(found->second.getBaseValue(), target.component);

        auto expression = baseExpressions_.find(target);
        if (expression != baseExpressions_.end() && expression->second)
            parameter->setExpression(*expression->second, target.component);
    }

    baseExpressions_.erase(target);
    tracks_.erase(found);

    // The value no longer reads the time, so the engine does not have to move
    // it when the scene moves.
    clearTimeDependency(target);

    trackChanged(target);
    tracksChanged();
}

void KeyframeStore::removeNode(nt::NodeId nodeId)
{
    std::vector<KeyframeTarget> removed;
    for (const auto& [target, track] : tracks_)
        if (target.nodeId == nodeId) removed.push_back(target);

    for (const KeyframeTarget& target : removed)
    {
        tracks_.erase(target);
        baseExpressions_.erase(target);
        clearTimeDependency(target);
        trackChanged(target);
    }
    if (!removed.empty()) tracksChanged();
}

void KeyframeStore::clear()
{
    if (tracks_.empty()) return;

    for (const auto& [target, track] : tracks_) clearTimeDependency(target);

    tracks_.clear();
    baseExpressions_.clear();
    tracksChanged();
}

const KeyframeTrack* KeyframeStore::getTrack(const KeyframeTarget& target) const
{
    auto found = tracks_.find(target);
    if (found == tracks_.end()) return nullptr;
    return &found->second;
}

bool KeyframeStore::hasTrack(const KeyframeTarget& target) const
{
    return tracks_.find(target) != tracks_.end();
}

std::optional<floatT> KeyframeStore::evaluate(const KeyframeTarget& target, floatT frame) const
{
    const KeyframeTrack* track = getTrack(target);
    if (!track) return std::nullopt;
    return track->evaluate(frame);
}

std::vector<KeyframeTarget> KeyframeStore::getTargets() const
{
    std::vector<KeyframeTarget> targets;
    targets.reserve(tracks_.size());
    for (const auto& [target, track] : tracks_)
        targets.push_back(target);
    return targets;
}

std::vector<KeyframeTarget> KeyframeStore::getTargets(nt::NodeId nodeId) const
{
    std::vector<KeyframeTarget> targets;
    for (const auto& [target, track] : tracks_)
        if (target.nodeId == nodeId) targets.push_back(target);
    return targets;
}

std::optional<floatT> KeyframeStore::getBaseValue(const KeyframeTarget& target) const
{
    const KeyframeTrack* track = getTrack(target);
    if (!track) return std::nullopt;
    return track->getBaseValue();
}

void KeyframeStore::applyToParameters(floatT frame)
{
    for (const auto& [target, track] : tracks_)
    {
        auto parameter = findParameter(target);
        if (!parameter) continue;

        // Only a float parameter can be animated. An int or a string keeps the
        // value it has.
        if (parameter->getValueType() != prm::ValueType::Float) continue;
        if (target.component >= parameter->getVectorSize()) continue;

        const floatT value = track.evaluate(frame);

        // The stored value is compared rather than the evaluated one, since a
        // parameter holding an expression evaluates to something else from the
        // value the track drives.
        const prm::PrmValues& values = parameter->getValues();
        const auto* floats = std::get_if<std::vector<floatT>>(&values);
        if (!floats) continue;
        if (target.component >= floats->size()) continue;
        if (std::abs((*floats)[target.component] - value) <= kValueEpsilon) continue;

        parameter->setFloat(value, target.component);
    }
}

KeyframeStore::KeyframeRecord KeyframeStore::makeRecord_(const KeyframeTarget& target) const
{
    KeyframeRecord record;
    record.nodeId = target.nodeId;
    if (nt::nm().isValidNode(target.nodeId))
        record.nodePath = nt::nm().getNode(target.nodeId).getPath().getString();
    record.parameter = target.parameter;
    record.component = target.component;
    record.baseValue = tracks_.at(target).getBaseValue();

    auto expression = baseExpressions_.find(target);
    if (expression != baseExpressions_.end() && expression->second)
        record.baseExpression = *expression->second;

    record.keyframes = tracks_.at(target).getKeyframes();
    return record;
}

void KeyframeStore::readRecords_()
{
    tracks_.clear();
    baseExpressions_.clear();

    for (const KeyframeRecord& record : records_)
    {
        // The node ids are handed out again when a scene is read, so the path
        // is what finds the node the keys belong to.
        nt::NodeId nodeId = record.nodeId;
        if (!record.nodePath.empty())
        {
            bool found = false;
            for (auto [candidateId, node] : nt::nm().nodes())
            {
                if (node.getPath().getString() != record.nodePath) continue;
                nodeId = candidateId;
                found = true;
                break;
            }
            if (!found) continue;
        }

        KeyframeTarget target{nodeId, record.parameter, record.component};

        KeyframeTrack track(record.baseValue);
        for (const Keyframe& key : record.keyframes)
            track.addKeyframe(key.frame, key.value, key.interpolation);
        tracks_[target] = std::move(track);

        if (!record.baseExpression.empty()) baseExpressions_[target] = record.baseExpression;
    }

    tracksChanged();
}

floatT resolveParameter(
    nt::NodeId nodeId,
    const std::string& parameter,
    unsigned int component,
    floatT frame
)
{
    // A value that holds keyframes gives what its track holds at the frame.
    const KeyframeTarget target{nodeId, parameter, component};
    if (auto animated = KeyframeStore::instance().evaluate(target, frame))
        return *animated;

    // Any other value gives what it holds now, which is what its own
    // expression works out to.
    if (!nt::nm().isValidNode(nodeId)) return 0.f;
    auto parameter_ = nt::nm().getNode(nodeId).getParameter(parameter).lock();
    if (!parameter_) return 0.f;
    return parameter_->evalFloat(component);
}

} // namespace helix::sim