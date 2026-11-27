#include "Engine/Network/CookCache.h"
#include <algorithm>
#include <cmath>

namespace helix::nt {

CookCache& CookCache::instance()
{
    static CookCache cache;
    return cache;
}

void CookCache::setBudget(std::size_t bytes)
{
    // What is evicted is released after the lock, since freeing a big mesh is not quick.
    std::map<Id, Item> evicted;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        budget_ = bytes;
        if (budget_ == 0)
        {
            evicted.swap(items_);
            recency_.clear();
            bytes_ = 0;
        }
        else
        {
            evictLocked_();
        }
    }
}

std::size_t CookCache::budget() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return budget_;
}

bool CookCache::enabled() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return budget_ > 0;
}

CookCacheStats CookCache::stats() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    CookCacheStats stats;
    stats.bytesUsed = bytes_;
    stats.budget = budget_;
    stats.entries = items_.size();
    for (const auto& [id, item] : items_)
    {
        if (id.slot == 0) ++stats.frameEntries;
        else ++stats.stateEntries;
    }
    stats.hits = hits_;
    stats.misses = misses_;
    stats.stateHits = stateHits_;
    return stats;
}

void CookCache::clear()
{
    std::map<Id, Item> evicted;
    const std::lock_guard<std::mutex> lock(mutex_);
    evicted.swap(items_);
    recency_.clear();
    bytes_ = 0;
}

void CookCache::purgeNode(NodeId nodeId)
{
    std::vector<Item> evicted;
    const std::lock_guard<std::mutex> lock(mutex_);
    auto item = items_.lower_bound(Id{nodeId, 0, 0, std::numeric_limits<std::int64_t>::min()});
    while (item != items_.end() && item->first.node == nodeId)
    {
        evicted.push_back(std::move(item->second));
        bytes_ -= evicted.back().bytes;
        recency_.erase(evicted.back().recency);
        item = items_.erase(item);
    }
}

std::shared_ptr<const CachedCook> CookCache::findFrame(NodeId nodeId, std::uint64_t key)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto item = items_.find(Id{nodeId, 0, key, 0});
    if (item == items_.end())
    {
        ++misses_;
        return nullptr;
    }

    ++hits_;
    recency_.splice(recency_.begin(), recency_, item->second.recency);
    return std::static_pointer_cast<const CachedCook>(item->second.payload);
}

void CookCache::storeFrame(
    NodeId nodeId,
    std::uint64_t key,
    std::shared_ptr<const CachedCook> cook,
    std::size_t bytes
)
{
    Item item;
    item.frame = cook->frame;
    item.bytes = bytes;
    item.payload = std::move(cook);
    insert_(Id{nodeId, 0, key, 0}, std::move(item));
}

std::vector<int> CookCache::cachedFrames(NodeId nodeId) const
{
    std::vector<int> frames;
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto item = items_.lower_bound(Id{nodeId, 0, 0, std::numeric_limits<std::int64_t>::min()});
         item != items_.end() && item->first.node == nodeId && item->first.slot == 0;
         ++item)
    {
        const floatT frame = item->second.frame;
        if (frame == std::floor(frame)) frames.push_back(static_cast<int>(frame));
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

std::optional<CachedState>
CookCache::findState(NodeId nodeId, std::uint32_t slot, std::uint64_t key, std::int64_t maxIndex)
{
    const std::lock_guard<std::mutex> lock(mutex_);

    // The first entry past the index, so the one before it is the latest at or before.
    slot = slot == 0 ? 1u : slot;
    auto item = items_.upper_bound(Id{nodeId, slot, key, maxIndex});
    if (item == items_.begin()) return std::nullopt;
    --item;
    if (item->first.node != nodeId || item->first.slot != slot || item->first.key != key)
        return std::nullopt;

    ++stateHits_;
    recency_.splice(recency_.begin(), recency_, item->second.recency);
    return CachedState{item->first.index, item->second.payload};
}

void CookCache::storeState(
    NodeId nodeId,
    std::uint32_t slot,
    std::uint64_t key,
    std::int64_t index,
    std::shared_ptr<const void> state,
    std::size_t bytes
)
{
    Item item;
    item.bytes = bytes;
    item.payload = std::move(state);
    insert_(Id{nodeId, slot == 0 ? 1u : slot, key, index}, std::move(item));
}

void CookCache::insert_(const Id& id, Item item)
{
    // Whatever leaves the cache is released after the lock.
    std::vector<Item> released;
    {
        const std::lock_guard<std::mutex> lock(mutex_);

        // A cook that alone is larger than the budget would only push everything else out.
        if (budget_ == 0 || item.bytes > budget_) return;

        auto existing = items_.find(id);
        if (existing != items_.end())
        {
            released.push_back(std::move(existing->second));
            bytes_ -= released.back().bytes;
            recency_.erase(released.back().recency);
            items_.erase(existing);
        }

        recency_.push_front(id);
        item.recency = recency_.begin();
        bytes_ += item.bytes;
        items_.emplace(id, std::move(item));

        // The entries used longest ago go first.
        while (bytes_ > budget_ && !recency_.empty())
        {
            auto victim = items_.find(recency_.back());
            if (victim == items_.end())
            {
                recency_.pop_back();
                continue;
            }
            released.push_back(std::move(victim->second));
            bytes_ -= released.back().bytes;
            recency_.pop_back();
            items_.erase(victim);
        }
    }
}

void CookCache::evictLocked_()
{
    while (bytes_ > budget_ && !recency_.empty())
    {
        auto victim = items_.find(recency_.back());
        recency_.pop_back();
        if (victim == items_.end()) continue;
        bytes_ -= victim->second.bytes;
        items_.erase(victim);
    }
}

} // namespace helix::nt
