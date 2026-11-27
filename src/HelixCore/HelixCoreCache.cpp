// The cook cache half of the facade. See the Cook cache section of HelixCore.h.

#include "Engine/Network/CookCache.h"
#include "Engine/Network/NetworkManager.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <cstdint>
#include <vector>

namespace helix::core {

CookCacheInfo cookCacheInfo()
{
    const nt::CookCacheStats stats = nt::cookCache().stats();
    CookCacheInfo info;
    info.bytesUsed = stats.bytesUsed;
    info.budget = stats.budget;
    info.entries = stats.entries;
    info.frameEntries = stats.frameEntries;
    info.stateEntries = stats.stateEntries;
    info.hits = stats.hits;
    info.misses = stats.misses;
    info.stateHits = stats.stateHits;
    return info;
}

void setCookCacheBudget(std::uint64_t bytes)
{
    nt::cookCache().setBudget(static_cast<std::size_t>(bytes));
}

void clearCookCache() { nt::cookCache().clear(); }

std::vector<std::int32_t> cachedFrames(NodeId node)
{
    if (!detail::requireNodeInScene(node).ok) return {};

    std::vector<std::int32_t> frames;
    for (const int frame : nt::cookCache().cachedFrames(node))
        frames.push_back(static_cast<std::int32_t>(frame));
    return frames;
}

} // namespace helix::core
