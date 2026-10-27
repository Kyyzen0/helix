#include "Gui/Timeline/TimelineViewModel.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Simulation/KeyframeStore.h"
#include <QVariantMap>
#include <algorithm>

namespace helix::ui {

namespace {

// Returns the parameter a name points at on a node, or null when it is not
// there or cannot be keyframed.
std::shared_ptr<prm::NodeParameter> findAnimatable(
    const nt::NodeId nodeId,
    const QString& parameter
)
{
    if (!nt::nm().isValidNode(nodeId)) return nullptr;
    auto found = nt::nm().getNode(nodeId).getParameter(parameter.toStdString()).lock();
    if (!found) return nullptr;
    if (found->getValueType() != prm::ValueType::Float) return nullptr;
    return found;
}

// The names the components of a parameter are given, one per axis. A
// parameter holding a single value has no axes, so it is called the value.
QStringList componentLabels(unsigned int componentCount)
{
    static const QStringList axisNames{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    if (componentCount <= 1) return {QStringLiteral("Value")};
    if (componentCount <= axisNames.size()) return axisNames.mid(0, static_cast<int>(componentCount));
    return axisNames;
}

} // namespace

TimelineViewModel::TimelineViewModel(QObject* parent) : QObject(parent)
{
    frameSubscription_ = nt::nm().frameChanged.connect([this](floatT) { Q_EMIT frameChanged(); });
    frameRangeSubscription_ =
        nt::nm().frameRangeChanged.connect([this](intT, intT) { Q_EMIT frameRangeChanged(); });
    fpsSubscription_ = nt::nm().fpsChanged.connect([this](floatT) { Q_EMIT fpsChanged(); });

    // The keyframes moving or changing means the markers on the scale move.
    tracksSubscription_ = sim::KeyframeStore::instance().tracksChanged.connect([this]() {
        Q_EMIT keyframesChanged();
    });

    // A different node means a different set of parameters to keyframe.
    primaryNodeSubscription_ = nt::nm().primaryNodeChanged.connect([this](std::optional<nt::NodeId>) {
        clampKeyframeComponent_();
        Q_EMIT keyframesChanged();
        Q_EMIT keyframeParameterChanged();
        Q_EMIT keyframeComponentChanged();
    });
}

qreal TimelineViewModel::frame() const { return nt::nm().getFrame(); }

int TimelineViewModel::startFrame() const { return nt::nm().getStartFrame(); }

int TimelineViewModel::endFrame() const { return nt::nm().getEndFrame(); }

qreal TimelineViewModel::fps() const { return nt::nm().getFps(); }

void TimelineViewModel::setFrame(qreal frame) { nt::nm().setFrame(static_cast<floatT>(frame)); }

void TimelineViewModel::setStartFrame(int frame) { nt::nm().setStartFrame(frame); }

void TimelineViewModel::setEndFrame(int frame) { nt::nm().setEndFrame(frame); }

void TimelineViewModel::setFps(qreal fps) { nt::nm().setFps(static_cast<floatT>(fps)); }

nt::NodeId TimelineViewModel::keyframeNode() const
{
    // The node the parameter pane is showing is the one the keys belong to.
    if (auto primary = nt::nm().getPrimaryNode()) return *primary;

    const std::vector<nt::NodeId>& selected = nt::nm().getSelectedNodes();
    if (!selected.empty()) return selected.front();
    return nt::nullNode;
}

QStringList TimelineViewModel::animatableParameters() const
{
    QStringList names;
    const nt::NodeId nodeId = keyframeNode();
    if (!nt::nm().isValidNode(nodeId)) return names;

    for (const std::weak_ptr<prm::NodeParameter>& weak : nt::nm().getNode(nodeId).getParameters())
    {
        auto parameter = weak.lock();
        if (!parameter) continue;
        if (parameter->getValueType() != prm::ValueType::Float) continue;
        names.append(QString::fromStdString(parameter->getName()));
    }
    return names;
}

QString TimelineViewModel::keyframeParameter() const
{
    const QStringList names = animatableParameters();
    if (names.isEmpty()) return QString();

    if (!keyframeParameter_.isEmpty() && names.contains(keyframeParameter_))
        return keyframeParameter_;

    return names.first();
}

void TimelineViewModel::setKeyframeParameter(const QString& parameter)
{
    if (keyframeParameter_ == parameter) return;
    keyframeParameter_ = parameter;

    // The new parameter may hold fewer components than the old one, so the
    // chosen component is brought back inside it.
    clampKeyframeComponent_();

    Q_EMIT keyframeParameterChanged();
    Q_EMIT keyframeComponentChanged();
    Q_EMIT keyframesChanged();
}

std::shared_ptr<prm::NodeParameter> TimelineViewModel::keyframeParameterValue() const
{
    return findAnimatable(keyframeNode(), keyframeParameter());
}

int TimelineViewModel::keyframeComponent() const { return keyframeComponent_; }

void TimelineViewModel::setKeyframeComponent(int component)
{
    if (component < 0) component = 0;
    if (component == keyframeComponent_) return;

    keyframeComponent_ = component;
    clampKeyframeComponent_();

    Q_EMIT keyframeComponentChanged();
    Q_EMIT keyframesChanged();
}

int TimelineViewModel::keyframeComponentCount() const
{
    const auto parameter = keyframeParameterValue();
    if (!parameter) return 0;
    return static_cast<int>(parameter->getVectorSize());
}

QStringList TimelineViewModel::keyframeComponentLabels() const
{
    return componentLabels(static_cast<unsigned int>(std::max(0, keyframeComponentCount())));
}

void TimelineViewModel::clampKeyframeComponent_()
{
    const int count = keyframeComponentCount();
    if (count <= 0)
    {
        keyframeComponent_ = 0;
        return;
    }
    if (keyframeComponent_ >= count) keyframeComponent_ = count - 1;
}

QVariantList TimelineViewModel::keyframes() const
{
    QVariantList entries;
    const nt::NodeId nodeId = keyframeNode();
    const QString parameter = keyframeParameter();
    if (parameter.isEmpty()) return entries;

    const unsigned int component = static_cast<unsigned int>(keyframeComponent_);
    const sim::KeyframeTarget target{nodeId, parameter.toStdString(), component};
    const sim::KeyframeTrack* track = sim::KeyframeStore::instance().getTrack(target);
    if (!track) return entries;

    for (const sim::Keyframe& key : track->getKeyframes())
    {
        QVariantMap entry;
        entry.insert(QStringLiteral("frame"), static_cast<double>(key.frame));
        entry.insert(QStringLiteral("parameter"), parameter);
        entry.insert(QStringLiteral("component"), static_cast<int>(component));
        entry.insert(QStringLiteral("value"), static_cast<double>(key.value));
        entries.append(entry);
    }
    return entries;
}

bool TimelineViewModel::hasKeyframe() const
{
    const nt::NodeId nodeId = keyframeNode();
    const QString parameter = keyframeParameter();
    if (parameter.isEmpty()) return false;

    const unsigned int component = static_cast<unsigned int>(keyframeComponent());
    const sim::KeyframeTarget target{nodeId, parameter.toStdString(), component};
    const sim::KeyframeTrack* track = sim::KeyframeStore::instance().getTrack(target);
    if (!track) return false;
    return track->hasKeyframe(static_cast<floatT>(frame()));
}

void TimelineViewModel::toggleKeyframe()
{
    const nt::NodeId nodeId = keyframeNode();
    const QString parameter = keyframeParameter();
    auto target = findAnimatable(nodeId, parameter);
    if (!target) return;

    const unsigned int component = static_cast<unsigned int>(keyframeComponent());
    const floatT atFrame = static_cast<floatT>(frame());
    const sim::KeyframeTarget keyTarget{nodeId, parameter.toStdString(), component};

    if (hasKeyframe())
    {
        sim::KeyframeStore::instance().removeKeyframe(keyTarget, atFrame);
        return;
    }

    // The key pins the value the parameter holds right now, which is the value
    // the track would give at this frame anyway.
    const floatT value = target->evalFloat(component);
    sim::KeyframeStore::instance().setKeyframe(keyTarget, atFrame, value);
}

void TimelineViewModel::clearKeyframes()
{
    const nt::NodeId nodeId = keyframeNode();
    const QString parameter = keyframeParameter();
    if (parameter.isEmpty()) return;

    const unsigned int component = static_cast<unsigned int>(keyframeComponent());
    sim::KeyframeStore::instance().clearTrack({nodeId, parameter.toStdString(), component});
}

double TimelineViewModel::keyframeValue() const
{
    auto target = keyframeParameterValue();
    if (!target) return 0.0;
    return static_cast<double>(target->evalFloat(static_cast<unsigned int>(keyframeComponent())));
}

} // namespace helix::ui
