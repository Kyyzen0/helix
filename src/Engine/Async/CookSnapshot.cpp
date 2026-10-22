#include "Engine/Async/CookSnapshot.h"

namespace enzo::async {

void CookSnapshot::setTime(floatT frame, floatT time)
{
    frame_ = frame;
    time_ = time;
}

void CookSnapshot::setNode(NodeState state)
{
    const nt::NodeId nodeId = state.nodeId;
    nodes_[nodeId] = std::move(state);
}

const CookSnapshot::NodeState* CookSnapshot::findNode(nt::NodeId nodeId) const
{
    auto node = nodes_.find(nodeId);
    if (node == nodes_.end()) return nullptr;
    return &node->second;
}

void CookSnapshot::setParameter(
    nt::NodeId nodeId,
    const std::string& name,
    CookParameterValue value
)
{
    NodeState& state = nodes_[nodeId];
    state.nodeId = nodeId;
    state.parameters[name] = std::move(value);
}

const CookParameterValue* CookSnapshot::findParameter(
    nt::NodeId nodeId,
    std::string_view name
) const
{
    const NodeState* state = findNode(nodeId);
    if (!state) return nullptr;

    auto parameter = state->parameters.find(std::string(name));
    if (parameter == state->parameters.end()) return nullptr;
    return &parameter->second;
}

} // namespace enzo::async
