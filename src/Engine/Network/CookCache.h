#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Network/NodePacket.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace helix::nt {

/// @brief What the cook cache holds and how it has been used, see nt::CookCache::info.
struct CookCacheStats
{
    /// @brief The estimated size of everything held, in bytes.
    std::size_t bytesUsed = 0;
    /// @brief The most the cache may hold, in bytes.
    std::size_t budget = 0;
    /// @brief How many things are held (finished cooks and solver checkpoints together).
    std::size_t entries = 0;
    /// @brief How many of them are the finished output of a node at one frame.
    std::size_t frameEntries = 0;
    /// @brief How many of them are the saved state of a solver at one frame.
    std::size_t stateEntries = 0;
    /// @brief How many cooks were answered from the cache since the process started.
    std::uint64_t hits = 0;
    /// @brief How many lookups found nothing.
    std::uint64_t misses = 0;
    /// @brief How many times a solver resumed from a saved state instead of its first frame.
    std::uint64_t stateHits = 0;
};

/// @brief One node's finished cook, as the cache keeps it.
struct CachedCook
{
    /// @brief What each output held, shared with the cook that made it.
    std::vector<std::shared_ptr<const NodePacket>> outputs;
    /// @brief What the cook warned about, so a cook answered from the cache says it again.
    std::vector<std::string> warnings;
    /// @brief Whether the cook read the scene time, so the node keeps moving with it.
    bool readsTime = false;
    /// @brief The frame the cook ran at.
    floatT frame = 0;
};

/// @brief A solver state the cache was asked to look up.
struct CachedState
{
    /// @brief How many whole frames the state sits after its start.
    std::int64_t index = 0;
    /// @brief What the solver saved there.
    std::shared_ptr<const void> state;
};

/**
 * @brief The memory of the cook: finished cooks of nodes that move with the time, and the saved
 * states of the solvers they run.
 *
 * Everything a time dependent node produced at a frame is kept, filed under a key that stands for
 * everything the cook depended on (see nt::CookCacheAttempt). Moving back to a frame already
 * visited then costs a lookup instead of a cook. The solvers of the simulation node also save
 * their state every few frames, so a frame not visited yet is reached from the nearest saved
 * state instead of from the first frame.
 *
 * One process wide budget bounds the whole thing. When it is exceeded the entry used longest ago
 * goes first, whatever node it belongs to. A budget of zero turns the cache off.
 *
 * @note Safe to use from several threads: every call takes the one lock, which is never held
 * while a node cooks.
 */
class CookCache
{
  public:
    /// @brief The budget a new process starts with, in bytes.
    static constexpr std::size_t kDefaultBudget = std::size_t{1} << 30;

    /// @brief The cache every cook of the process uses.
    static CookCache& instance();

    /// @brief Sets the most the cache may hold, evicting what no longer fits.
    /// @note Zero turns the cache off and empties it.
    void setBudget(std::size_t bytes);

    /// @brief The most the cache may hold, in bytes.
    std::size_t budget() const;

    /// @brief Whether the cache keeps anything at all.
    bool enabled() const;

    /// @brief What the cache holds now.
    CookCacheStats stats() const;

    /// @brief Forgets everything.
    void clear();

    /// @brief Forgets everything held for one node.
    void purgeNode(NodeId nodeId);

    /// @brief Returns the cook filed under a key, or null. Counts as a use.
    std::shared_ptr<const CachedCook> findFrame(NodeId nodeId, std::uint64_t key);

    /// @brief Files a finished cook under a key, replacing the one that was there.
    /// @param bytes The estimated size of what the cook holds, see NodePacket::estimateBytes.
    void storeFrame(
        NodeId nodeId,
        std::uint64_t key,
        std::shared_ptr<const CachedCook> cook,
        std::size_t bytes
    );

    /// @brief The whole frames a node has a cook for, in order, whatever the key it was filed under.
    std::vector<int> cachedFrames(NodeId nodeId) const;

    /// @brief Returns the state a solver saved the latest at or before an index, or nothing.
    /// @param slot Tells the states of one node's solvers apart.
    /// @param key Stands for everything the solver depends on except the frame.
    std::optional<CachedState>
    findState(NodeId nodeId, std::uint32_t slot, std::uint64_t key, std::int64_t maxIndex);

    /// @brief Files the state of a solver after a number of whole frames.
    void storeState(
        NodeId nodeId,
        std::uint32_t slot,
        std::uint64_t key,
        std::int64_t index,
        std::shared_ptr<const void> state,
        std::size_t bytes
    );

  private:
    CookCache() = default;

    // Where an entry sits. Slot zero is a finished cook, the other slots are solver states.
    struct Id
    {
        NodeId node = 0;
        std::uint32_t slot = 0;
        std::uint64_t key = 0;
        std::int64_t index = 0;

        auto operator<=>(const Id& other) const = default;
    };

    struct Item
    {
        std::shared_ptr<const void> payload;
        std::size_t bytes = 0;
        floatT frame = 0;
        std::list<Id>::iterator recency;
    };

    // Files an entry and evicts what no longer fits. Takes the lock itself.
    void insert_(const Id& id, Item item);
    // Evicts the entries used longest ago until the budget holds. Assumes the lock is held.
    void evictLocked_();

    mutable std::mutex mutex_;
    std::map<Id, Item> items_;
    // Most recently used first.
    std::list<Id> recency_;
    std::size_t bytes_ = 0;
    std::size_t budget_ = kDefaultBudget;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t stateHits_ = 0;
};

/// @brief The cache every cook of the process uses.
inline CookCache& cookCache() { return CookCache::instance(); }

} // namespace helix::nt
