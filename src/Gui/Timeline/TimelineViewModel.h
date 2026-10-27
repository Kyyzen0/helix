#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Parameter/NodeParameter.h"
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <boost/signals2/connection.hpp>

namespace helix::ui {

/// @brief View-model backing the timeline bar.
///
/// Exposes the scene frame, the playback range and the frame rate as Qt
/// properties.
///
/// It also carries the keyframes of the node the parameter pane is showing, so
/// the timeline can draw them and write them.
class TimelineViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(int startFrame READ startFrame WRITE setStartFrame NOTIFY frameRangeChanged)
    Q_PROPERTY(int endFrame READ endFrame WRITE setEndFrame NOTIFY frameRangeChanged)
    Q_PROPERTY(qreal fps READ fps WRITE setFps NOTIFY fpsChanged)

    // The parameters of the node being shown that can be keyframed.
    Q_PROPERTY(QStringList animatableParameters READ animatableParameters NOTIFY keyframesChanged)

    // The parameter the keyframe buttons work on.
    Q_PROPERTY(
        QString keyframeParameter READ keyframeParameter WRITE setKeyframeParameter NOTIFY
            keyframeParameterChanged
    )

    // Which component of that parameter the keyframe buttons work on. A
    // parameter that holds one value has a single component.
    Q_PROPERTY(
        int keyframeComponent READ keyframeComponent WRITE setKeyframeComponent NOTIFY
            keyframeComponentChanged
    )

    // How many components that parameter has, so the interface knows whether
    // there is a component to pick.
    Q_PROPERTY(int keyframeComponentCount READ keyframeComponentCount NOTIFY keyframesChanged)

    // The names of the components, one per component of the parameter.
    Q_PROPERTY(QStringList keyframeComponentLabels READ keyframeComponentLabels NOTIFY keyframesChanged)

    // The keyframes of that parameter and component, one entry per key.
    Q_PROPERTY(QVariantList keyframes READ keyframes NOTIFY keyframesChanged)

    // Whether that parameter holds a key on the frame the scene sits on.
    Q_PROPERTY(bool hasKeyframe READ hasKeyframe NOTIFY keyframesChanged)

  public:
    explicit TimelineViewModel(QObject* parent = nullptr);

    /// @brief Returns the frame the scene sits on.
    qreal frame() const;

    /// @brief Returns the first frame of the playback range.
    int startFrame() const;

    /// @brief Returns the last frame of the playback range.
    int endFrame() const;

    /// @brief Returns the playback frame rate.
    qreal fps() const;

    void setFrame(qreal frame);
    void setStartFrame(int frame);
    void setEndFrame(int frame);
    void setFps(qreal fps);

    /// @brief Returns the names of the parameters that can be keyframed.
    QStringList animatableParameters() const;

    /// @brief Returns the parameter the keyframe buttons work on.
    QString keyframeParameter() const;

    /// @brief Sets the parameter the keyframe buttons work on.
    void setKeyframeParameter(const QString& parameter);

    /// @brief Returns which component of that parameter the buttons work on.
    int keyframeComponent() const;

    /// @brief Sets which component of that parameter the buttons work on.
    void setKeyframeComponent(int component);

    /// @brief Returns how many components that parameter has.
    int keyframeComponentCount() const;

    /// @brief Returns the names of the components of that parameter.
    QStringList keyframeComponentLabels() const;

    /// @brief Returns the keyframes of that parameter, one entry per key.
    ///
    /// Each entry holds the frame, the parameter, the component, and the value
    /// the key pins.
    QVariantList keyframes() const;

    /// @brief Returns whether that parameter holds a key on the current frame.
    bool hasKeyframe() const;

    /// @brief Puts a key on the current frame, or takes the one there away.
    Q_INVOKABLE void toggleKeyframe();

    /// @brief Takes every key off that parameter away.
    Q_INVOKABLE void clearKeyframes();

    /// @brief Returns the value that parameter holds on the current frame.
    Q_INVOKABLE double keyframeValue() const;

  Q_SIGNALS:
    void frameChanged();
    void frameRangeChanged();
    void fpsChanged();
    void keyframesChanged();
    void keyframeParameterChanged();
    void keyframeComponentChanged();

  private:
    /// @brief Returns the node the keyframes belong to.
    helix::nt::NodeId keyframeNode() const;

    /// @brief Returns the parameter the keyframe buttons work on.
    std::shared_ptr<prm::NodeParameter> keyframeParameterValue() const;

    /// @brief Brings the chosen component back inside the parameter it belongs to.
    void clampKeyframeComponent_();

    boost::signals2::scoped_connection frameSubscription_;
    boost::signals2::scoped_connection frameRangeSubscription_;
    boost::signals2::scoped_connection fpsSubscription_;
    boost::signals2::scoped_connection tracksSubscription_;
    boost::signals2::scoped_connection primaryNodeSubscription_;

    // The parameter the keyframe buttons last worked on.
    QString keyframeParameter_;

    // Which component of that parameter the buttons work on.
    int keyframeComponent_ = 0;
};

} // namespace helix::ui
