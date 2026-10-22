#pragma once

#include "Engine/Core/Types.h"
#include "Engine/Network/NodePacket.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace enzo::async {

/**
 * @class CookResult
 * @brief The geometry one cook produced, and when it is ready to be shown.
 *
 * A cook writes into the result as it goes, one entry per node output. The
 * result is also what the cook reads from, so a node always sees the finished
 * geometry of the nodes it depends on rather than a half written one.
 *
 * The result is published as a whole. The interface keeps the result of the
 * last cook that finished and only swaps in a new one once that cook is done,
 * so what is on screen is never a mixture of two cooks.
 *
 * @note Safe to use from several threads at once. The lock it keeps is only
 * held while an entry is read or written, never while a node cooks.
 */
class CookResult
{
  public:
    /// @brief Stores the geometry one node output produced.
    void setOutput(
        nt::NodeId nodeId,
        unsigned int outputIndex,
        std::shared_ptr<const NodePacket> packet
    );

    /// @brief Returns the geometry one node output produced, or null when it produced none.
    std::shared_ptr<const NodePacket> getOutput(nt::NodeId nodeId, unsigned int outputIndex) const;

    /// @brief Notes that this cook cooked a node.
    void markCooked(nt::NodeId nodeId);

    /// @brief Notes that a node read the scene time while it cooked.
    void markTimeDependent(nt::NodeId nodeId);

    /// @brief Returns whether a node read the scene time while it cooked.
    bool isTimeDependent(nt::NodeId nodeId) const;

    /// @brief Returns whether the result holds no geometry at all.
    bool empty() const;

    /// @brief Returns every node this cook cooked, in no particular order.
    std::vector<nt::NodeId> cookedNodes() const;

    /// @brief Returns every node that read the scene time, in no particular order.
    std::vector<nt::NodeId> timeDependentNodes() const;

  private:
    // The lock guards both maps, so a node reading an input and a node
    // finishing its cook never step on each other.
    mutable std::mutex mutex_;

    // The geometry each node produced, keyed by node id and then by output.
    std::unordered_map<nt::NodeId, std::vector<std::shared_ptr<const NodePacket>>> outputs_;

    // The nodes this cook cooked, which is not the same as the nodes it holds
    // geometry for, since a clean node keeps the geometry it already had.
    std::unordered_set<nt::NodeId> cooked_;

    // The nodes that read the scene time while they cooked.
    std::unordered_set<nt::NodeId> timeDependent_;
};

} // namespace enzo::async
