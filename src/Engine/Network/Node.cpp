#include "Engine/Network/Node.h"
#include "Engine/Network/CookContext.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/StyleAccess.h"
#include "Engine/Parameter/Template.h"
#include "Engine/Primitives/Primitive.h"
#include "icecream.hpp"
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace helix {

namespace {

// A parsed comparison such as "applyscale == 0", read from a disable or hide
// condition string.
struct ParameterComparison
{
    std::string controller;
    std::string comparator;
    intT target;

    // Whether the controller's value satisfies the comparison.
    bool isMet(intT value) const
    {
        const bool isEqual = value == target;
        if (comparator == "!=") return !isEqual;
        return isEqual;
    }
};

// Reads a comparison string, empty when it is blank or malformed.
std::optional<ParameterComparison> parseParameterComparison(const std::string& text)
{
    std::istringstream stream(text);
    ParameterComparison comparison;
    if (stream >> comparison.controller >> comparison.comparator >> comparison.target)
        return comparison;
    return std::nullopt;
}

} // namespace

nt::Node::Node(nt::NodeId nodeId, const nt::NodeType& nodeType, const Path& path)
    : nodeId_{nodeId}, nodeType_{nodeType}, path_{path}
{
    // Start every output on an empty packet so consumers never see null.
    outputPackets_.resize(getMaxOutputs());
    for (auto& packet : outputPackets_)
        packet = std::make_shared<const NodePacket>();

    initParameters();
}

void nt::Node::initParameters()
{
    // Extract parameters from groups
    std::function<void(const prm::Template&)> visit = [&](const prm::Template& templateEntry) {
        if (templateEntry.getType() == prm::Type::GROUP)
        {
            for (const prm::Template& child : templateEntry.getChildren())
                visit(child);
            return;
        }

        auto parameter = std::make_shared<prm::NodeParameter>(templateEntry, nodeId_);
        parameter->valueChanged.connect([this, name = templateEntry.getName()]() {
            onParameterChanged(name);
        });
        parameters_.push_back(parameter);
    };

    for (const prm::Template& templateEntry : nodeType_.templates)
        visit(templateEntry);
}

void nt::Node::dirtyNode(bool dirtyDescendents)
{
    dirty_ = true;
    // What the node said about its last cook is out of date once it is dirty. Leaving it would
    // show an error for a value that has since been corrected, on a node nothing cooks again.
    clearDiagnostics();
    nodeDirtied(nodeId_, dirtyDescendents);
}

void nt::Node::onParameterChanged(const std::string& parmName)
{
    parameterChanged(parmName);
    dirtyNode();
}

bool nt::Node::isDirty() { return dirty_; }

void nt::Node::cook(nt::CookContext& context)
{
    if (!dirty_) return;

    clearDiagnostics();
    // An output the implementation does not set stays an empty packet (see NodeImpl::cook), so
    // it must not keep what an older cook put there.
    resetOutputPackets();

    try
    {
        // The implementation lives only as long as the cook, so nothing it holds
        // can survive into the next one.
        const std::unique_ptr<nt::NodeImpl> implementation(nodeType_.ctorFunc(*this, context));
        implementation->cook();
    }
    catch (const std::exception& error)
    {
        // A node that fails is reported on that node, and the rest of the scene keeps working.
        const std::string message = error.what();
        recordError(message.empty() ? "The node failed without a message." : message);
        resetOutputPackets();
    }
    catch (...)
    {
        recordError("The node failed without a message.");
        resetOutputPackets();
    }
    dirty_ = false;
}

std::string nt::Node::getError() const
{
    const std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    return error_;
}

std::vector<std::string> nt::Node::getWarnings() const
{
    const std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    return warnings_;
}

void nt::Node::recordError(std::string error)
{
    const std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    if (error_.empty()) error_ = std::move(error);
}

void nt::Node::recordWarning(std::string warning)
{
    const std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    warnings_.push_back(std::move(warning));
}

void nt::Node::clearDiagnostics()
{
    const std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    error_.clear();
    warnings_.clear();
}

void nt::Node::resetOutputPackets()
{
    for (auto& packet : outputPackets_)
        packet = std::make_shared<const NodePacket>();
}

std::shared_ptr<const NodePacket> nt::Node::getOutputPacket(unsigned outputIndex) const
{
    if (outputIndex >= getMaxOutputs())
    {
        throw std::runtime_error("Cannot get output packet at an index past the node's outputs");
    }

    return outputPackets_.at(outputIndex);
}

void nt::Node::setOutputPacket(unsigned int outputIndex, NodePacket packet)
{
    if (outputIndex >= getMaxOutputs())
    {
        throw std::runtime_error("Cannot set output packet at an index past the node's outputs");
    }

    // Defragment every primitive so downstream consumers see contiguous offsets.
    for (auto& prim : packet.getPrimitives())
    {
        if (prim) prim->defragment();
    }

    outputPackets_[outputIndex] = std::make_shared<const NodePacket>(std::move(packet));
}

bool nt::Node::outputRequested(unsigned int outputIndex) const
{
    // TODO: implement
    return true;
}

std::weak_ptr<prm::NodeParameter> nt::Node::getParameter(std::string_view parameterName)
{
    for (auto parm : parameters_)
    {
        if (parm->getName() == parameterName)
        {
            return parm;
        }
    }
    return std::weak_ptr<prm::NodeParameter>();
}

bool nt::Node::isComparisonTrue(const std::string& conditionText)
{
    const std::optional<ParameterComparison> comparison = parseParameterComparison(conditionText);
    if (!comparison) return false;

    // The controlling parameter the comparison reads from.
    auto controller = getParameter(comparison->controller);
    if (controller.expired()) return false;

    return comparison->isMet(controller.lock()->evalInt());
}

bool nt::Node::isParameterEnabled(std::string_view parmName)
{
    // An unknown parameter is treated as enabled.
    auto parameter = getParameter(parmName);
    if (parameter.expired()) return true;

    // Disabled only while its disable comparison is true.
    return !isComparisonTrue(parameter.lock()->getTemplate().getDisableCondition());
}

bool nt::Node::isParameterHidden(std::string_view parmName)
{
    // An unknown parameter is treated as shown.
    auto parameter = getParameter(parmName);
    if (parameter.expired()) return false;

    // Hidden only while its hide comparison is true.
    return isComparisonTrue(parameter.lock()->getTemplate().getHideCondition());
}

bool nt::Node::isTemplateHidden(const prm::Template& templateEntry)
{
    return isComparisonTrue(templateEntry.getHideCondition());
}

bool nt::Node::isTemplateEnabled(const prm::Template& templateEntry)
{
    return !isComparisonTrue(templateEntry.getDisableCondition());
}

std::optional<std::string> nt::Node::getReferencedValue(const std::string& text)
{
    const std::optional<std::string> name = prm::style::getReferencedParameterName(text);
    if (!name) return text;

    const std::weak_ptr<prm::NodeParameter> referenced = getParameter(*name);
    if (referenced.expired()) return std::nullopt;

    return referenced.lock()->evalString();
}

std::vector<std::weak_ptr<prm::NodeParameter>> nt::Node::getParameters()
{
    return {parameters_.begin(), parameters_.end()};
}

const std::vector<prm::Template>& nt::Node::getTemplates() const { return nodeType_.templates; }

std::string nt::Node::getName() const { return path_.getName(); }

const nt::NodeType& nt::Node::getType() const { return nodeType_; }

// std::optional<nt::NodeId> nt::Node::getInput(unsigned int inputNumber) const
// {
//     if(inputNumber>=maxInputs_)
//     {
//         return std::nullopt;
//     }
//     return inputIds_.at(inputNumber);
// }

// std::optional<nt::NodeId> nt::Node::getOutput(unsigned int outputNumber) const
// {
//     if(outputNumber>=maxOutputs_)
//     {
//         return std::nullopt;
//     }
//     return outputIds_.at(outputNumber);
// }

bool nt::Node::takesInput() const { return !nodeType_.inputPorts.empty(); }
unsigned int nt::Node::getMaxOutputs() const { return nodeType_.maxOutputs; }

} // namespace helix
