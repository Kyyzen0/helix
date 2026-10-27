#include "Engine/Async/CookPlan.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace helix::async {

CookPlan CookPlan::build(
    const std::vector<nt::NodeId>& targets,
    const std::function<std::vector<nt::NodeLink>(nt::NodeId)>& inputsOf,
    const std::function<unsigned int(nt::NodeId)>& inputCountOf
)
{
    CookPlan plan;

    // Walk up from the targets to find every node the cook has to cover.
    // The map keeps the walk in a fixed order, so the plan never depends on
    // the order the graph happens to hand its links over in.
    std::map<nt::NodeId, std::vector<nt::NodeLink>> inputs;
    std::vector<nt::NodeId> pending(targets.begin(), targets.end());

    while (!pending.empty())
    {
        const nt::NodeId nodeId = pending.back();
        pending.pop_back();

        if (inputs.count(nodeId)) continue;

        std::vector<nt::NodeLink> nodeInputs = inputsOf(nodeId);
        inputs.emplace(nodeId, nodeInputs);

        for (const nt::NodeLink& nodeLink : nodeInputs)
            pending.push_back(nodeLink.sourceNode);
    }

    // Count, for each node, how many of its feeders are also part of the cook.
    // A node that feeds another through two inputs still counts once, so the
    // levels are computed over the shape of the graph and not over its wiring.
    std::map<nt::NodeId, std::set<nt::NodeId>> feeders;
    for (const auto& [nodeId, nodeInputs] : inputs)
    {
        for (const nt::NodeLink& nodeLink : nodeInputs)
            if (inputs.count(nodeLink.sourceNode)) feeders[nodeId].insert(nodeLink.sourceNode);
    }

    // A node with no feeder left is ready to cook, so it starts a level.
    std::map<nt::NodeId, unsigned int> waitingOn;
    for (const auto& [nodeId, nodeInputs] : inputs)
        waitingOn[nodeId] = static_cast<unsigned int>(feeders[nodeId].size());

    std::vector<nt::NodeId> ready;
    for (const auto& [nodeId, count] : waitingOn)
        if (count == 0) ready.push_back(nodeId);

    size_t placed = 0;
    while (!ready.empty())
    {
        // Sort the level so the plan is the same however the graph was walked.
        std::sort(ready.begin(), ready.end());
        plan.levels_.push_back(ready);

        for (const nt::NodeId nodeId : ready)
        {
            NodeEntry entry;
            entry.nodeId = nodeId;
            entry.inputs = inputs.at(nodeId);
            entry.inputCount = inputCountOf(nodeId);
            plan.entries_[nodeId] = std::move(entry);
        }
        placed += ready.size();

        // The nodes these feed can cook once this level is done.
        std::vector<nt::NodeId> nextReady;
        for (const nt::NodeId nodeId : ready)
        {
            for (const auto& [otherId, otherFeeders] : feeders)
            {
                if (!otherFeeders.count(nodeId)) continue;
                if (--waitingOn[otherId] == 0) nextReady.push_back(otherId);
            }
        }
        ready = std::move(nextReady);
    }

    if (placed != inputs.size())
        throw std::runtime_error("Dependency cycle through the node network");

    return plan;
}

const CookPlan::NodeEntry* CookPlan::find(nt::NodeId nodeId) const
{
    auto entry = entries_.find(nodeId);
    if (entry == entries_.end()) return nullptr;
    return &entry->second;
}

std::vector<nt::NodeId> CookPlan::nodes() const
{
    std::vector<nt::NodeId> nodeIds;
    nodeIds.reserve(entries_.size());
    for (const std::vector<nt::NodeId>& level : levels_)
        nodeIds.insert(nodeIds.end(), level.begin(), level.end());
    return nodeIds;
}

} // namespace helix::async
