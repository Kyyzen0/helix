#include "Engine/Async/CookPlan.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace helix::async {

CookPlan CookPlan::build(
    const std::vector<nt::NodeId>& targets,
    const std::function<std::vector<nt::NodeLink>(nt::NodeId)>& inputsOf,
    const std::function<unsigned int(nt::NodeId)>& inputCountOf
)
{
    CookPlan plan;

    // Walk up from the targets to find every node the cook has to cover.
    // The levels are sorted below, so the plan never depends on the order the
    // walk or the graph happens to hand its links over in.
    std::unordered_map<nt::NodeId, std::vector<nt::NodeLink>> inputs;
    std::vector<nt::NodeId> pending(targets.begin(), targets.end());

    while (!pending.empty())
    {
        const nt::NodeId nodeId = pending.back();
        pending.pop_back();

        if (inputs.count(nodeId)) continue;

        std::vector<nt::NodeLink> nodeInputs = inputsOf(nodeId);
        for (const nt::NodeLink& nodeLink : nodeInputs)
            pending.push_back(nodeLink.sourceNode);
        inputs.emplace(nodeId, std::move(nodeInputs));
    }

    // Count, for each node, how many of its feeders are also part of the cook,
    // and record the reverse edges once so a finished node only has to visit
    // the nodes it feeds. A node that feeds another through two inputs still
    // counts once, so the levels are computed over the shape of the graph and
    // not over its wiring.
    std::unordered_map<nt::NodeId, unsigned int> waitingOn;
    std::unordered_map<nt::NodeId, std::vector<nt::NodeId>> dependents;
    waitingOn.reserve(inputs.size());
    dependents.reserve(inputs.size());
    std::vector<nt::NodeId> feeders;
    for (const auto& [nodeId, nodeInputs] : inputs)
    {
        feeders.clear();
        for (const nt::NodeLink& nodeLink : nodeInputs)
            if (inputs.count(nodeLink.sourceNode)) feeders.push_back(nodeLink.sourceNode);
        std::sort(feeders.begin(), feeders.end());
        feeders.erase(std::unique(feeders.begin(), feeders.end()), feeders.end());

        waitingOn[nodeId] = static_cast<unsigned int>(feeders.size());
        for (const nt::NodeId feeder : feeders)
            dependents[feeder].push_back(nodeId);
    }

    // A node with no feeder left is ready to cook, so it starts a level.
    std::vector<nt::NodeId> ready;
    for (const auto& [nodeId, count] : waitingOn)
        if (count == 0) ready.push_back(nodeId);

    plan.entries_.reserve(inputs.size());
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
            const auto fed = dependents.find(nodeId);
            if (fed == dependents.end()) continue;
            for (const nt::NodeId otherId : fed->second)
                if (--waitingOn[otherId] == 0) nextReady.push_back(otherId);
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
