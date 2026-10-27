#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Simulation/Keyframe.h"
#include <boost/signals2.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace helix::sim {

/**
 * @brief Names the one animated value a track belongs to.
 *
 * A target is a node, one of its parameters, and one component of that
 * parameter. The y of a translate is one target, and the x is another, so the
 * two animate independently.
 */
struct KeyframeTarget
{
    nt::NodeId nodeId = nt::nullNode;
    std::string parameter;
    unsigned int component = 0;

    bool operator==(const KeyframeTarget& other) const = default;

    /// @brief Orders targets so they can live in a sorted map.
    bool operator<(const KeyframeTarget& other) const
    {
        if (nodeId != other.nodeId) return nodeId < other.nodeId;
        if (parameter != other.parameter) return parameter < other.parameter;
        return component < other.component;
    }
};

/**
 * @brief Holds the keyframes of every animated value in the scene.
 *
 * The store is the one place a keyframe lives. It knows which parameter each
 * track belongs to, and it keeps the value the parameter held before the track
 * was made, so removing the track puts the parameter back as it was.
 *
 * The store listens to the scene moving to a new frame. When it does, every
 * track is resolved at that frame and the result is written into the
 * parameter it belongs to, which dirties the node and makes the engine cook it
 * again. A keyframed parameter therefore reads its animated value the same way
 * any other parameter reads its value, with no special case in the cook.
 *
 * @note A singleton, since the tracks belong to the scene rather than to any
 * one part of it.
 *
 * Example
 *
 * ```
 * auto& store = helix::sim::KeyframeStore::instance();
 * store.setKeyframe({nodeId, "translate", 1}, 1.f, 0.f);
 * store.setKeyframe({nodeId, "translate", 1}, 24.f, 10.f);
 * store.evaluate({nodeId, "translate", 1}, 12.5f); // 4.807692
 * ```
 */
class KeyframeStore
{
  public:
    /// @brief Returns the store holding the scene's keyframes.
    static KeyframeStore& instance();

    /**
     * @brief Adds a keyframe, or replaces the one already sitting on that frame.
     *
     * The first keyframe on a parameter captures the value the parameter held,
     * so removing every key later can put it back.
     *
     * @param target The value the keyframe animates.
     * @param frame The frame the key sits on.
     * @param value The value at that frame.
     * @param interpolation How the value moves from this keyframe to the next.
     */
    void setKeyframe(
        const KeyframeTarget& target,
        floatT frame,
        floatT value,
        Interpolation interpolation = Interpolation::Linear
    );

    /**
     * @brief Removes the keyframe sitting on a frame.
     * @return True when a keyframe was there to remove.
     */
    bool removeKeyframe(const KeyframeTarget& target, floatT frame);

    /// @brief Removes every keyframe of one value, putting its old value back.
    void clearTrack(const KeyframeTarget& target);

    /// @brief Removes every keyframe belonging to a node.
    void removeNode(nt::NodeId nodeId);

    /// @brief Removes every keyframe in the scene.
    void clear();

    /// @brief The track of a value, or null when it holds no keyframes.
    const KeyframeTrack* getTrack(const KeyframeTarget& target) const;

    /// @brief Whether a value holds any keyframes.
    bool hasTrack(const KeyframeTarget& target) const;

    /**
     * @brief Resolves the value of an animated value at a frame.
     * @return The value at that frame, or nothing when the value holds no
     * keyframes and keeps its own value.
     */
    std::optional<floatT> evaluate(const KeyframeTarget& target, floatT frame) const;

    /// @brief Every value that holds keyframes, in a stable order.
    std::vector<KeyframeTarget> getTargets() const;

    /// @brief Every value of one node that holds keyframes, in a stable order.
    std::vector<KeyframeTarget> getTargets(nt::NodeId nodeId) const;

    /// @brief The value a parameter held before its track was made.
    std::optional<floatT> getBaseValue(const KeyframeTarget& target) const;

    /**
     * @brief Resolves every track at a frame and writes the results into the
     * parameters they belong to.
     *
     * A parameter whose value did not change is left alone, so a frame that
     * moves without changing anything does not dirty the scene.
     *
     * @param frame The frame to resolve at.
     */
    void applyToParameters(floatT frame);

    /// @brief A signal emitted whenever the set of keyframes changes.
    boost::signals2::signal<void()> tracksChanged;

    /// @brief A signal emitted whenever one value's keyframes change.
    boost::signals2::signal<void(const KeyframeTarget&)> trackChanged;

    /// @brief Reads and writes the keyframes, so they travel with a scene.
    template <class Archive> void serialize(Archive& ar)
    {
        // The archive says which way it goes, because the keyframes are
        // written out in a form the read has to rebuild. The check is made on
        // the type of the archive, since the archives of cereal carry it as a
        // member type rather than as a function.
        if constexpr (Archive::is_loading::value)
        {
            ar(CEREAL_NVP(records_));
            readRecords_();
            return;
        }

        records_.clear();
        for (const auto& [target, track] : tracks_)
            records_.push_back(makeRecord_(target));
        ar(CEREAL_NVP(records_));
    }

  private:
    KeyframeStore();

    /// @brief One value's keyframes, in the form they are written out in.
    struct KeyframeRecord
    {
        nt::NodeId nodeId = nt::nullNode;

        // The path of the node the value belongs to. The node ids are handed
        // out again when a scene is read, so the path is what tells one
        // node's keys from another's.
        std::string nodePath;

        std::string parameter;
        unsigned int component = 0;
        floatT baseValue = 0;
        std::string baseExpression;
        std::vector<Keyframe> keyframes;

        template <class Archive> void serialize(Archive& ar)
        {
            ar(
                CEREAL_NVP(nodeId),
                CEREAL_NVP(nodePath),
                CEREAL_NVP(parameter),
                CEREAL_NVP(component),
                CEREAL_NVP(baseValue),
                CEREAL_NVP(baseExpression),
                CEREAL_NVP(keyframes)
            );
        }
    };

    /// @brief Builds the record of a target from its track.
    KeyframeRecord makeRecord_(const KeyframeTarget& target) const;

    /// @brief Takes the records apart again after a scene is read.
    void readRecords_();

    /// @brief The keyframes of every animated value, ordered by target.
    std::map<KeyframeTarget, KeyframeTrack> tracks_;

    /// @brief The expression each parameter held before its track was made.
    std::map<KeyframeTarget, std::optional<String>> baseExpressions_;

    /// @brief The records the keyframes are written out as.
    std::vector<KeyframeRecord> records_;

    // Holds the subscriptions to the engine's signals.
    boost::signals2::scoped_connection frameSubscription_;
    boost::signals2::scoped_connection nodeRemovedSubscription_;
    boost::signals2::scoped_connection networkClearedSubscription_;
};

/**
 * @brief Returns the value of a parameter at a frame, taking keyframes into
 * account.
 *
 * A parameter that holds keyframes gives the value its track holds at that
 * frame. Any other parameter gives the value it holds now, which is what its
 * own expression works out to.
 *
 * @param nodeId The node the parameter belongs to.
 * @param parameter The name of the parameter.
 * @param component The component of the parameter.
 * @param frame The frame to resolve at.
 * @return The value of the parameter at that frame.
 */
floatT resolveParameter(
    nt::NodeId nodeId,
    const std::string& parameter,
    unsigned int component,
    floatT frame
);

} // namespace helix::sim
