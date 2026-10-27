#include "Engine/Network/CookContext.h"
#include "Engine/Async/CookPlan.h"
#include "Engine/Async/CookResult.h"
#include "Engine/Async/CookSnapshot.h"
#include "Engine/Async/CookTypes.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Ramp.h"
#include <memory>
#include <stdexcept>

namespace helix {

nt::CookContext::CookContext(nt::NodeId nodeId, nt::NetworkManager& networkManager)
    : nodeId_{nodeId}, networkManager_{networkManager}
{
}

nt::CookContext::CookContext(
    nt::NodeId nodeId,
    nt::NetworkManager& networkManager,
    const async::CookSnapshot& snapshot,
    const async::CookPlan& plan,
    async::CookResult& result,
    const async::CookCancelToken& cancelToken
)
    : nodeId_{nodeId}
    , networkManager_{networkManager}
    , snapshot_{&snapshot}
    , plan_{&plan}
    , result_{&result}
    , cancelToken_{&cancelToken}
{
}

NodePacket nt::CookContext::cloneInputPacket(unsigned int inputIndex)
{
    return getInputPacket(inputIndex)->deepCopy();
}

std::shared_ptr<const NodePacket> nt::CookContext::getInputPacket(unsigned int inputIndex)
{
    // A cook that reads a snapshot takes its wiring from the plan and its
    // geometry from the result, so it never reads the graph and never waits
    // for another node to cook.
    if (snapshot_)
    {
        const async::CookPlan::NodeEntry* entry = plan_->find(nodeId_);
        if (!entry) return std::make_shared<const NodePacket>();

        for (const nt::NodeLink& nodeLink : entry->inputs)
        {
            if (nodeLink.targetInput != inputIndex) continue;
            std::shared_ptr<const NodePacket> packet =
                result_->getOutput(nodeLink.sourceNode, nodeLink.sourceOutput);
            if (packet) return packet;
            return std::make_shared<const NodePacket>();
        }

        return std::make_shared<const NodePacket>();
    }

    auto inputNodeLink = networkManager_.graph().getInputNodeLink(nodeId_, inputIndex);
    if (!inputNodeLink) return std::make_shared<const NodePacket>();

    networkManager_.cook(inputNodeLink->sourceNode);
    return networkManager_.getNode(inputNodeLink->sourceNode)
        .getOutputPacket(inputNodeLink->sourceOutput);
}

bool nt::CookContext::hasInput(unsigned int inputIndex)
{
    if (snapshot_)
    {
        const async::CookPlan::NodeEntry* entry = plan_->find(nodeId_);
        if (!entry) return false;

        for (const nt::NodeLink& nodeLink : entry->inputs)
            if (nodeLink.targetInput == inputIndex) return true;

        return false;
    }

    return networkManager_.graph().getInputNodeLink(nodeId_, inputIndex).has_value();
}

unsigned int nt::CookContext::getInputCount()
{
    if (snapshot_)
    {
        const async::CookPlan::NodeEntry* entry = plan_->find(nodeId_);
        if (!entry) return 0;
        return entry->inputCount;
    }

    return networkManager_.getInputCount(nodeId_);
}

bool nt::CookContext::isCancelled() const
{
    if (!cancelToken_) return false;
    return cancelToken_->isCancelled();
}

void nt::CookContext::recordTimeDependency_() const
{
    // The graph belongs to the thread that owns the network, so a cook on a
    // worker thread leaves the note with the result and the network picks it
    // up when the cook is published.
    if (snapshot_)
    {
        result_->markTimeDependent(nodeId_);
        return;
    }

    networkManager_.graph().setTimeDependent(nt::Unit{nodeId_}, true);
}

floatT nt::CookContext::getFrame() const
{
    recordTimeDependency_();
    if (snapshot_) return snapshot_->frame();
    return networkManager_.getFrame();
}

floatT nt::CookContext::getTime() const
{
    recordTimeDependency_();
    if (snapshot_) return snapshot_->time();
    return networkManager_.getTime();
}

const async::CookParameterValue* nt::CookContext::findParameterValue_(std::string_view parmName) const
{
    if (!snapshot_) return nullptr;
    return snapshot_->findParameter(nodeId_, parmName);
}

floatT nt::CookContext::snapshotFloat_(
    const async::CookParameterValue& value,
    unsigned int index
) const
{
    if (index < value.floats.size()) return value.floats[index];
    return 0;
}

intT nt::CookContext::snapshotInt_(const async::CookParameterValue& value, unsigned int index) const
{
    if (index < value.ints.size()) return value.ints[index];
    return 0;
}

String nt::CookContext::snapshotString_(
    const async::CookParameterValue& value,
    unsigned int index
) const
{
    if (index < value.strings.size()) return value.strings[index];
    return String();
}

// TODO: cache value
floatT nt::CookContext::evalParmFloat(std::string_view parmName, const unsigned int index) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return snapshotFloat_(*value, index);
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalFloat(index);
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

// TODO: cache value
intT nt::CookContext::evalParmInt(std::string_view parmName, const unsigned int index) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return snapshotInt_(*value, index);
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalInt(index);
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

// TODO: cache value
boolT nt::CookContext::evalParmBool(std::string_view parmName, const unsigned int index) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return snapshotInt_(*value, index);
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalInt(index);
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

// TODO: cache value
String nt::CookContext::evalParmString(std::string_view parmName, const unsigned int index) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return snapshotString_(*value, index);
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalString(index);
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

prm::Ramp nt::CookContext::evalParmRamp(std::string_view parmName) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        if (value->ramp) return *value->ramp;
        return prm::Ramp();
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return prm::Ramp(*sharedParm);
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

std::vector<floatT> nt::CookContext::evalParmFloats(std::string_view parmName) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return value->floats;
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalFloats();
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

Vector2 nt::CookContext::evalParmVector2(std::string_view parmName) const
{
    std::vector<floatT> components = evalParmFloats(parmName);
    Vector2 result = Vector2::Zero();
    for (size_t componentIndex = 0; componentIndex < components.size() && componentIndex < 2;
         ++componentIndex)
    {
        result[componentIndex] = components[componentIndex];
    }
    return result;
}

Vector3 nt::CookContext::evalParmVector3(std::string_view parmName) const
{
    std::vector<floatT> components = evalParmFloats(parmName);
    Vector3 result = Vector3::Zero();
    for (size_t componentIndex = 0; componentIndex < components.size() && componentIndex < 3;
         ++componentIndex)
    {
        result[componentIndex] = components[componentIndex];
    }
    return result;
}

std::vector<intT> nt::CookContext::evalParmInts(std::string_view parmName) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return value->ints;
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalInts();
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

std::vector<String> nt::CookContext::evalParmStrings(std::string_view parmName) const
{
    if (snapshot_)
    {
        const async::CookParameterValue* value = findParameterValue_(parmName);
        if (!value)
            throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
        return value->strings;
    }

    nt::Node& selfNode = networkManager_.getNode(nodeId_);
    std::weak_ptr<prm::NodeParameter> parameter = selfNode.getParameter(parmName);

    if (auto sharedParm = parameter.lock())
    {
        return sharedParm->evalStrings();
    }
    else
    {
        throw std::runtime_error(std::string(parmName) + " parameter does not exist.");
    }
}

} // namespace helix
