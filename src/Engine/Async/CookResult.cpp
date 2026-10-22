#include "Engine/Async/CookResult.h"

namespace enzo::async {

void CookResult::setOutput(
    nt::NodeId nodeId,
    unsigned int outputIndex,
    std::shared_ptr<const NodePacket> packet
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<const NodePacket>>& outputs = outputs_[nodeId];
    if (outputs.size() <= outputIndex) outputs.resize(outputIndex + 1);
    outputs[outputIndex] = std::move(packet);
}

std::shared_ptr<const NodePacket> CookResult::getOutput(
    nt::NodeId nodeId,
    unsigned int outputIndex
) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto node = outputs_.find(nodeId);
    if (node == outputs_.end()) return nullptr;

    const std::vector<std::shared_ptr<const NodePacket>>& outputs = node->second;
    if (outputIndex >= outputs.size()) return nullptr;

    return outputs[outputIndex];
}

void CookResult::markCooked(nt::NodeId nodeId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cooked_.insert(nodeId);
}

void CookResult::markTimeDependent(nt::NodeId nodeId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timeDependent_.insert(nodeId);
}

bool CookResult::isTimeDependent(nt::NodeId nodeId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return timeDependent_.count(nodeId) > 0;
}

bool CookResult::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return outputs_.empty();
}

std::vector<nt::NodeId> CookResult::cookedNodes() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return std::vector<nt::NodeId>(cooked_.begin(), cooked_.end());
}

std::vector<nt::NodeId> CookResult::timeDependentNodes() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return std::vector<nt::NodeId>(timeDependent_.begin(), timeDependent_.end());
}

} // namespace enzo::async
