#pragma once

#include "Engine/Core/Types.h"
#include "Engine/NetworkGraph/NodeLink.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace enzo::async {

/**
 * @class CookPlan
 * @brief The nodes one cook covers and the order they are cooked in.
 *
 * A plan is built once, on the thread that owns the network, before any worker
 * touches it. Workers then read the plan and never the graph, so the graph is
 * only ever read while no cook is running.
 *
 * The plan holds the nodes grouped into levels. Every node in a level reads
 * only nodes in earlier levels, so a whole level can be cooked at the same
 * time as the next one cannot start until the level before it has finished.
 *
 * @note Nodes in one level are sorted by id, so the same network always gives
 * the same plan and a cook gives the same result however the work is spread
 * over threads.
 */
class CookPlan
{
  public:
    /**
     * @brief What one node in the plan needs while it cooks.
     */
    struct NodeEntry
    {
        /// @brief The node this entry covers.
        nt::NodeId nodeId = nt::nullNode;

        /// @brief The node links feeding this node, in input order.
        std::vector<nt::NodeLink> inputs;

        /// @brief How many inputs the node takes.
        unsigned int inputCount = 0;
    };

    /**
     * @brief Builds a plan covering @p targets and everything feeding them.
     *
     * @param targets The nodes the cook has to produce a result for.
     * @param inputsOf Returns the node links feeding a node, in input order.
     * @param inputCountOf Returns how many inputs a node takes.
     *
     * @throws std::runtime_error when the wiring holds a cycle.
     */
    static CookPlan build(
        const std::vector<nt::NodeId>& targets,
        const std::function<std::vector<nt::NodeLink>(nt::NodeId)>& inputsOf,
        const std::function<unsigned int(nt::NodeId)>& inputCountOf
    );

    /// @brief The nodes to cook, grouped into levels.
    const std::vector<std::vector<nt::NodeId>>& levels() const { return levels_; }

    /// @brief Returns what the plan knows about one node, or null when it covers no such node.
    const NodeEntry* find(nt::NodeId nodeId) const;

    /// @brief Returns every node the plan covers, in level order.
    std::vector<nt::NodeId> nodes() const;

    /// @brief Returns how many nodes the plan covers.
    size_t size() const { return entries_.size(); }

  private:
    // The nodes to cook, grouped so a whole level can run at once.
    std::vector<std::vector<nt::NodeId>> levels_;
    // What each node needs, keyed by node id.
    std::unordered_map<nt::NodeId, NodeEntry> entries_;
};

} // namespace enzo::async
