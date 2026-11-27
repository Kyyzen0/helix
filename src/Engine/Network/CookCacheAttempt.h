#pragma once
#include "Engine/Network/CookContext.h"
#include <cstdint>
#include <optional>

namespace helix::nt {
class Node;

/**
 * @brief One node's use of the cook cache for one cook.
 *
 * Before a node cooks, an attempt works out a key that stands for everything the cook depends on,
 * and asks the cache (see nt::CookCache) whether that cook was already done. If it was, the node
 * takes the outputs of that cook and does not cook. If it was not, the node cooks and the attempt
 * files what it produced.
 *
 * @par What the key holds
 * The type of the node, the identity of the packet that arrives at every input (see
 * NodePacket::identity), and the value of every parameter as the cook would read it, an expression
 * evaluated. Then everything that comes from the scene without being a parameter or an input: the
 * keyframes of the node, since a simulation reads a keyed setting at frames before the current
 * one, and the frame, the time and the frame rate. Two cooks with the same key read the same
 * values, so they produce the same geometry (the cooks are deterministic, see VISION.md).
 *
 * @par What is cached
 * Only a node that moves with the scene time: it reads the frame or the time, one of its
 * parameters does (a keyframe, an expression with `frame()`), or something wired into it does.
 * What does not move with the time is already kept by the node itself, and filing every
 * version of it while a slider is dragged would push out the frames that matter.
 *
 * @par Where it does not apply
 * A cook on a worker thread reads a snapshot of the network, not the live one (see
 * CookContext), and the parameters it would key on are not there to read. Those cooks leave the
 * cache alone.
 */
class CookCacheAttempt
{
  public:
    /// @brief Works out the key of the cook that is about to run.
    /// @return Nothing when the cache is off, the cook reads a snapshot, or a value of the node
    /// could not be read, in which case the node cooks without the cache.
    static std::optional<CookCacheAttempt> begin(Node& node, CookContext& context);

    /// @brief Gives the node the cook the cache holds for this key, if it holds one.
    /// @return True when the node took it, and the cook must not run.
    bool tryRestore(Node& node, CookContext& context);

    /// @brief Records what the cook that ran made, and files it when the node moves with the time.
    /// @note Does nothing for a cook that failed or was abandoned, and never throws.
    void finish(Node& node, CookContext& context);

    /// @brief The key of everything the cook depends on except the frame. A solver files the
    /// states it saves under it.
    std::uint64_t stateKey() const { return stateKey_; }

  private:
    /// @brief Works out the keys: what arrives at every input and every parameter the cook reads.
    /// @note Throws when a value cannot be read.
    void computeKeys_(Node& node, CookContext& context);

    /// @brief The key of the cook at the frame the scene sits on.
    std::uint64_t frameKey_(CookContext& context) const;

    bool cacheable_ = true;
    bool inputsVary_ = false;
    // Whether the keys are worked out. They are only worked out for a node that may move with the
    // time, since they cost the evaluation of every parameter, and a node that was never seen
    // reading the time gets them after its cook instead.
    bool keysReady_ = false;
    std::uint64_t stateKey_ = 0;
    std::uint64_t variableKey_ = 0;
};

} // namespace helix::nt
