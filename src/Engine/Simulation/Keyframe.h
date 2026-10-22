#pragma once
#include "Engine/Core/Types.h"
#include <algorithm>
#include <cmath>
#include <cereal/cereal.hpp>
#include <vector>

namespace enzo::sim {

/// @brief How a value moves from one keyframe to the next.
///
/// The mode belongs to the keyframe it starts from, so a track can hold a
/// steady section and a smooth one side by side.
enum class Interpolation
{
    /// @brief The value holds until the next keyframe, then jumps.
    Step,
    /// @brief The value moves at a constant rate between the two keyframes.
    Linear,
    /// @brief The value eases in and out between the two keyframes.
    Smooth,
};

/// @brief One point in time where an animated value is pinned.
///
/// @note The frame may be fractional, so a keyframe can sit between two whole
/// frames.
struct Keyframe
{
    /// @brief The frame the key sits on.
    floatT frame = 0;

    /// @brief The value at that frame.
    floatT value = 0;

    /// @brief How the value moves from this keyframe to the next one.
    Interpolation interpolation = Interpolation::Linear;

    bool operator==(const Keyframe& other) const = default;

    /// @brief Reads and writes the keyframe, so a track can travel with a scene.
    template <class Archive> void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(frame), CEREAL_NVP(value), CEREAL_NVP(interpolation));
    }
};

/**
 * @brief The keyframes of one component of one parameter, and the value it
 * falls back on when it holds none.
 *
 * A track is a pure function of the frame it is given. The same track and the
 * same frame always give the same value, whatever order the frames were asked
 * for in, so scrubbing back and forth gives the same answer as playing
 * forwards.
 *
 * Example
 *
 * ```
 * enzo::sim::KeyframeTrack track;
 * track.addKeyframe(1.f, 0.f, enzo::sim::Interpolation::Linear);
 * track.addKeyframe(11.f, 10.f, enzo::sim::Interpolation::Linear);
 * track.evaluate(6.f); // 5
 * ```
 */
class KeyframeTrack
{
  public:
    /// @brief Builds a track that holds no keyframes and falls back on a value.
    explicit KeyframeTrack(floatT baseValue = 0) : baseValue_{baseValue} {}

    /// @brief The value the track falls back on when it holds no keyframes.
    floatT getBaseValue() const { return baseValue_; }

    /// @brief Sets the value the track falls back on when it holds no keyframes.
    void setBaseValue(floatT value) { baseValue_ = value; }

    /// @brief The keyframes, ordered by frame.
    const std::vector<Keyframe>& getKeyframes() const { return keyframes_; }

    /// @brief Whether the track holds no keyframes.
    bool isEmpty() const { return keyframes_.empty(); }

    /// @brief The number of keyframes the track holds.
    std::size_t size() const { return keyframes_.size(); }

    /**
     * @brief Adds a keyframe, or replaces the one already sitting on that frame.
     *
     * @param frame The frame the key sits on.
     * @param value The value at that frame.
     * @param interpolation How the value moves from this keyframe to the next.
     */
    void addKeyframe(floatT frame, floatT value, Interpolation interpolation = Interpolation::Linear)
    {
        Keyframe key{frame, value, interpolation};
        auto existing = std::find_if(
            keyframes_.begin(),
            keyframes_.end(),
            [frame](const Keyframe& candidate) { return candidate.frame == frame; }
        );
        if (existing != keyframes_.end())
        {
            *existing = key;
            return;
        }
        keyframes_.push_back(key);
        sort_();
    }

    /**
     * @brief Removes the keyframe sitting on a frame.
     * @return True when a keyframe was there to remove.
     */
    bool removeKeyframe(floatT frame)
    {
        const auto removed = std::remove_if(
            keyframes_.begin(),
            keyframes_.end(),
            [frame](const Keyframe& candidate) { return candidate.frame == frame; }
        );
        if (removed == keyframes_.end()) return false;
        keyframes_.erase(removed, keyframes_.end());
        return true;
    }

    /// @brief Removes every keyframe, leaving the base value behind.
    void clear() { keyframes_.clear(); }

    /// @brief Whether a keyframe sits on the given frame.
    bool hasKeyframe(floatT frame) const
    {
        return std::any_of(
            keyframes_.begin(),
            keyframes_.end(),
            [frame](const Keyframe& candidate) { return candidate.frame == frame; }
        );
    }

    /// @brief The keyframe sitting on a frame, or null when none does.
    const Keyframe* findKeyframe(floatT frame) const
    {
        for (const Keyframe& key : keyframes_)
            if (key.frame == frame) return &key;
        return nullptr;
    }

    /**
     * @brief Resolves the value of the track at a frame.
     *
     * A frame before the first keyframe holds that keyframe's value, and a
     * frame after the last one holds the last keyframe's value, so the track
     * extends past its ends rather than dropping to the base value.
     *
     * @param frame The frame to resolve at.
     * @return The value at that frame.
     */
    floatT evaluate(floatT frame) const
    {
        if (keyframes_.empty()) return baseValue_;

        if (frame <= keyframes_.front().frame) return keyframes_.front().value;
        if (frame >= keyframes_.back().frame) return keyframes_.back().value;

        // The segment between two keys is found by walking to the key before
        // the frame. The tracks hold few keys, so a plain walk stays cheap.
        const Keyframe* left = &keyframes_.front();
        const Keyframe* right = &keyframes_.front();
        for (std::size_t index = 1; index < keyframes_.size(); ++index)
        {
            if (keyframes_[index].frame >= frame)
            {
                right = &keyframes_[index];
                break;
            }
            left = &keyframes_[index];
        }

        if (left->interpolation == Interpolation::Step) return left->value;

        const floatT span = right->frame - left->frame;
        if (span <= 0) return right->value;
        const floatT fraction = (frame - left->frame) / span;

        if (left->interpolation == Interpolation::Smooth)
        {
            // A smoothstep, so the value eases in and out of the segment.
            const floatT eased = fraction * fraction * (3.f - 2.f * fraction);
            return left->value + (right->value - left->value) * eased;
        }

        return left->value + (right->value - left->value) * fraction;
    }

  private:
    void sort_()
    {
        std::sort(
            keyframes_.begin(),
            keyframes_.end(),
            [](const Keyframe& left, const Keyframe& right) { return left.frame < right.frame; }
        );
    }

    // The keyframes, ordered by frame.
    std::vector<Keyframe> keyframes_;

    // The value the track falls back on when it holds no keyframes.
    floatT baseValue_ = 0;
};

} // namespace enzo::sim
