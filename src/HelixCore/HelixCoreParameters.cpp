// The parameter half of the facade that a panel needs: the description of every parameter with
// its range, options, defaults, expressions and whether it is hidden or enabled now, the order
// the parameters are shown in, expressions, reset, ramps and the attribute names a parameter can
// offer. The plain reads and writes of values are in HelixCore.cpp. See the Parameters section
// of HelixCore.h.

#include "Engine/Attribute/Attribute.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/StyleAccess.h"
#include "Engine/Parameter/Template.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

// An expression is compiled and run on every cook, so a pasted novel is refused.
constexpr std::size_t maxExpressionLength = 4096;
// A ramp is a handful of control points. The cap keeps a script from asking for a million.
constexpr std::size_t maxRampPoints = 64;
constexpr std::size_t minRampPoints = 2;

ParameterType toParameterType(prm::Type type)
{
    switch (type)
    {
    case prm::Type::FLOAT:
        return ParameterType::Float;
    case prm::Type::INT:
        return ParameterType::Int;
    case prm::Type::STRING:
        return ParameterType::String;
    case prm::Type::BOOL:
    case prm::Type::TOGGLE:
        return ParameterType::Toggle;
    case prm::Type::DROPDOWN:
        return ParameterType::Dropdown;
    case prm::Type::RAMP:
        return ParameterType::Ramp;
    case prm::Type::GROUP:
    case prm::Type::DIVIDER:
        return ParameterType::Other;
    }
    return ParameterType::Other;
}

// What the groups around a parameter say about it.
struct Surroundings
{
    bool hidden = false;
    bool enabled = true;
    std::vector<std::string> groups;
};

std::string joined(const std::vector<std::string>& labels)
{
    std::string text;
    for (const std::string& label : labels)
    {
        if (!text.empty()) text += " / ";
        text += label;
    }
    return text;
}

// The layout of a node, and what the groups around each parameter make of it.
struct Walk
{
    std::vector<ParameterLayoutItem> items;
    std::unordered_map<std::string, Surroundings> byParameter;
};

void walk(nt::Node& node, const std::vector<prm::Template>& templates, Surroundings around, Walk& out)
{
    for (const prm::Template& entry : templates)
    {
        // A hidden or disabled group makes everything inside it so, however its members are set.
        Surroundings here = around;
        here.hidden = around.hidden || node.isTemplateHidden(entry);
        here.enabled = around.enabled && node.isTemplateEnabled(entry);

        ParameterLayoutItem item;
        item.name = entry.getName();
        item.label = entry.getLabel();
        item.hidden = here.hidden;
        item.enabled = here.enabled;

        switch (entry.getType())
        {
        case prm::Type::GROUP:
        {
            item.kind = ParameterLayoutKind::GroupBegin;
            item.horizontal = entry.getDirection() == prm::Direction::HORIZONTAL;
            item.labelInline = entry.isLabelInline();
            out.items.push_back(item);

            Surroundings inside = here;
            inside.groups.push_back(entry.getLabel());
            walk(node, entry.getChildren(), inside, out);

            ParameterLayoutItem end = item;
            end.kind = ParameterLayoutKind::GroupEnd;
            out.items.push_back(end);
            break;
        }
        case prm::Type::DIVIDER:
            item.kind = ParameterLayoutKind::Divider;
            item.icon = entry.getIcon();
            out.items.push_back(item);
            // A divider is also a parameter of the node, so listParameters finds it here.
            out.byParameter[item.name] = here;
            break;
        default:
            item.kind = ParameterLayoutKind::Parameter;
            out.items.push_back(item);
            out.byParameter[item.name] = here;
            break;
        }
    }
}

Walk walkNode(nt::Node& node)
{
    Walk result;
    walk(node, node.getTemplates(), {}, result);
    return result;
}

// The default of a component, with the fallbacks the parameter itself applies when it is built.
prm::Default defaultOf(const prm::Template& entry, unsigned int component)
{
    if (component < entry.getNumDefaults()) return entry.getDefault(component);
    if (entry.getNumDefaults() == 1) return entry.getDefault();
    return prm::Default();
}

// The value of a style option as text, whatever type the option is.
std::string optionText(const prm::Parameter& option)
{
    switch (option.getValueType())
    {
    case prm::ValueType::Float:
        return std::to_string(option.evalFloat());
    case prm::ValueType::Int:
        return option.evalInt() != 0 ? "true" : "false";
    case prm::ValueType::String:
        return option.evalString();
    }
    return {};
}

bool takesExpression(prm::Type type)
{
    return type == prm::Type::FLOAT || type == prm::Type::INT || type == prm::Type::BOOL ||
           type == prm::Type::TOGGLE || type == prm::Type::STRING;
}

bool isAnimated(NodeId node, const std::string& parameter, std::uint32_t component)
{
    return sim::KeyframeStore::instance().hasTrack({node, parameter, component});
}

ParameterInfo describeParameter(
    NodeId node,
    prm::NodeParameter& parameter,
    const Surroundings& around
)
{
    const prm::Template& entry = parameter.getTemplate();
    ParameterInfo info;
    info.name = parameter.getName();
    info.label = parameter.getLabel();
    info.type = toParameterType(parameter.getType());
    info.componentCount = parameter.getVectorSize();
    info.tooltip = entry.getTooltip();
    info.documentation = entry.getDocumentation();
    info.labelHidden = entry.isLabelHidden();
    info.hidden = around.hidden;
    info.enabled = around.enabled;
    info.group = joined(around.groups);

    info.style = prm::style::toString(entry.getStyle());
    for (const std::shared_ptr<prm::Parameter>& option : prm::style::options(entry.getStyle()))
        info.styleOptions.push_back({option->getName(), optionText(*option)});

    const bool numeric = parameter.getType() == prm::Type::FLOAT || parameter.getType() == prm::Type::INT;
    if (numeric)
    {
        const prm::Range& range = entry.getRange(0);
        info.hasRange = true;
        info.rangeMin = range.getMin();
        info.rangeMax = range.getMax();
        info.minLocked = range.getMinFlag() == prm::RangeFlag::LOCKED;
        info.maxLocked = range.getMaxFlag() == prm::RangeFlag::LOCKED;
    }

    for (const prm::Name& option : entry.getOptions())
        info.options.push_back({option.getToken(), option.getLabel()});

    // Everything below is per component. A ramp keeps its points as instances, not components.
    if (parameter.getType() == prm::Type::RAMP) return info;

    for (unsigned int component = 0; component < info.componentCount; ++component)
    {
        const prm::Default fallback = defaultOf(entry, component);
        switch (parameter.getValueType())
        {
        case prm::ValueType::Float:
            info.defaultFloats.push_back(fallback.getFloat());
            break;
        case prm::ValueType::Int:
            info.defaultInts.push_back(fallback.getInt());
            break;
        case prm::ValueType::String:
            info.defaultStrings.push_back(fallback.getString());
            break;
        }

        info.expressions.push_back(parameter.getExpression(component).value_or(""));
        info.expressionErrors.push_back(parameter.getExpressionError(component).value_or(""));
        if (isAnimated(node, info.name, component)) info.animatedComponents.push_back(component);
    }
    return info;
}

Status findEditable(
    NodeId node,
    const std::string& name,
    std::uint32_t component,
    std::shared_ptr<prm::NodeParameter>& out
)
{
    if (Status status = detail::findNodeParameter(node, name, out); !status.ok) return status;
    if (out->getType() == prm::Type::RAMP || out->getType() == prm::Type::DIVIDER)
        return failure("The parameter '" + name + "' holds no value an expression can drive.");
    if (component >= out->getVectorSize())
        return failure(
            "Component " + std::to_string(component) + " is out of range, the parameter holds " +
            std::to_string(out->getVectorSize()) + "."
        );
    return success();
}

} // namespace

std::vector<ParameterInfo> listParameters(NodeId node)
{
    std::vector<ParameterInfo> parameters;
    if (!detail::requireNodeInScene(node).ok) return parameters;

    nt::Node& engineNode = nt::nm().getNode(node);
    const Walk layout = walkNode(engineNode);
    for (const std::weak_ptr<prm::NodeParameter>& weak : engineNode.getParameters())
    {
        const std::shared_ptr<prm::NodeParameter> parameter = weak.lock();
        if (!parameter) continue;
        const auto found = layout.byParameter.find(parameter->getName());
        const Surroundings around = found == layout.byParameter.end() ? Surroundings{} : found->second;
        parameters.push_back(describeParameter(node, *parameter, around));
    }
    return parameters;
}

std::vector<ParameterLayoutItem> listParameterLayout(NodeId node)
{
    if (!detail::requireNodeInScene(node).ok) return {};
    return walkNode(nt::nm().getNode(node)).items;
}

Status setExpression(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    const std::string& expression
)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findEditable(node, parameter, component, found); !status.ok) return status;
        if (!takesExpression(found->getType()))
            return failure("The parameter '" + parameter + "' takes one of its options, not an expression.");
        if (isAnimated(node, parameter, component))
            return failure(
                "The parameter '" + parameter + "' holds keyframes, and the timeline would overwrite an expression."
            );
        if (expression.find_first_not_of(" \t\r\n") == std::string::npos)
            return failure("An expression cannot be empty. Clear the expression instead.");
        if (expression.size() > maxExpressionLength)
            return failure(
                "The expression is longer than " + std::to_string(maxExpressionLength) + " characters."
            );
        found->setExpression(expression, component);
        detail::noteSceneModified();
        return success();
    });
}

Status clearExpression(NodeId node, const std::string& parameter, std::uint32_t component)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findEditable(node, parameter, component, found); !status.ok) return status;
        if (!found->hasExpression(component)) return success();
        found->clearExpression(component);
        detail::noteSceneModified();
        return success();
    });
}

Status resetParameter(NodeId node, const std::string& parameter)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = detail::findNodeParameter(node, parameter, found); !status.ok) return status;
        const prm::Template& entry = found->getTemplate();

        if (found->getType() == prm::Type::DIVIDER)
            return failure("The parameter '" + parameter + "' holds no value to reset.");

        if (found->getType() == prm::Type::RAMP)
        {
            while (found->getInstanceCount() > 0)
                found->removeInstance(found->getInstanceCount() - 1);
            const unsigned int count = entry.getDefault().getInt();
            for (unsigned int index = 0; index < count; ++index)
                found->addInstance();
            detail::noteSceneModified();
            return success();
        }

        for (std::uint32_t component = 0; component < found->getVectorSize(); ++component)
            if (isAnimated(node, parameter, component))
                return failure(
                    "The parameter '" + parameter + "' holds keyframes, which a reset would only be overwritten by."
                );

        for (unsigned int component = 0; component < found->getVectorSize(); ++component)
        {
            const prm::Default fallback = defaultOf(entry, component);
            // Writing a literal takes the component's expression off, so a reset clears both.
            switch (found->getValueType())
            {
            case prm::ValueType::Float:
                found->setFloat(fallback.getFloat(), component);
                break;
            case prm::ValueType::Int:
                found->setInt(fallback.getInt(), component);
                break;
            case prm::ValueType::String:
                found->setString(fallback.getString(), component);
                break;
            }
        }
        detail::noteSceneModified();
        return success();
    });
}

RampInfo getRamp(NodeId node, const std::string& parameter)
{
    RampInfo result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = detail::findNodeParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::RAMP)
            return failure("The parameter '" + parameter + "' is not a ramp.");

        for (unsigned int index = 0; index < found->getInstanceCount(); ++index)
        {
            RampPoint point;
            if (const auto field = found->getInstanceField(index, "position")) point.position = field->evalFloat();
            if (const auto field = found->getInstanceField(index, "value")) point.value = field->evalFloat();
            if (const auto field = found->getInstanceField(index, "interp")) point.interpolation = field->evalString();
            result.points.push_back(std::move(point));
        }
        for (const prm::Template& child : found->getTemplate().getChildren())
            if (child.getName() == "interp")
                for (const prm::Name& option : child.getOptions())
                    result.interpolations.push_back({option.getToken(), option.getLabel()});
        return success();
    });
    return result;
}

Status setRamp(NodeId node, const std::string& parameter, const std::vector<RampPoint>& points)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = detail::findNodeParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::RAMP)
            return failure("The parameter '" + parameter + "' is not a ramp.");

        if (points.size() < minRampPoints || points.size() > maxRampPoints)
            return failure(
                "A ramp has between " + std::to_string(minRampPoints) + " and " +
                std::to_string(maxRampPoints) + " points, not " + std::to_string(points.size()) + "."
            );

        // Everything is checked before anything is written, so a refused ramp changes nothing.
        std::set<std::string> known;
        for (const prm::Template& child : found->getTemplate().getChildren())
            if (child.getName() == "interp")
                for (const prm::Name& option : child.getOptions()) known.insert(option.getToken());
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const RampPoint& point = points[index];
            if (!std::isfinite(point.position) || !std::isfinite(point.value))
                return failure("Point " + std::to_string(index) + " of the ramp has a position or a value that is not a number.");
            if (!known.contains(point.interpolation))
                return failure(
                    "Point " + std::to_string(index) + " of the ramp has an unknown interpolation '" +
                    point.interpolation.substr(0, 40) + "'."
                );
        }

        while (found->getInstanceCount() > points.size())
            found->removeInstance(found->getInstanceCount() - 1);
        while (found->getInstanceCount() < points.size())
            found->addInstance();
        for (unsigned int index = 0; index < points.size(); ++index)
        {
            found->getInstanceField(index, "position")->setFloat(points[index].position);
            found->getInstanceField(index, "value")->setFloat(points[index].value);
            found->getInstanceField(index, "interp")->setString(points[index].interpolation);
        }
        detail::noteSceneModified();
        return success();
    });
}

AttributeNames listAttributeSuggestions(NodeId node, const std::string& parameter)
{
    AttributeNames result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = detail::findNodeParameter(node, parameter, found); !status.ok) return status;
        const auto style = std::any_cast<std::shared_ptr<prm::style::Attribute>>(&found->getTemplate().getStyle());
        if (!style) return failure("The parameter '" + parameter + "' does not name an attribute.");

        // A style option written as prm(name) reads another parameter of the node. One that is
        // empty or that names nothing the engine knows accepts everything, as the Qt panel did.
        nt::Node& engineNode = nt::nm().getNode(node);
        const auto read = [&](const std::string& text, auto parse) {
            const std::optional<std::string> resolved = engineNode.getReferencedValue(text);
            if (!resolved || resolved->empty()) return parse("all");
            try
            {
                return parse(*resolved);
            }
            catch (const std::runtime_error&)
            {
                return parse("all");
            }
        };
        const std::vector<attr::AttributeOwner> owners = read((*style)->owners(), prm::style::parseOwners);
        const std::vector<attr::AttributeType> types = read((*style)->attributeTypes(), prm::style::parseAttributeTypes);

        nt::NodeId source = 0;
        for (const nt::NodeLink& link : nt::nm().graph().getInputs(node))
            if (link.targetInput == 0) source = link.sourceNode;
        if (source == 0) return failure("Nothing is wired into the first input of the node.");

        std::shared_ptr<const NodePacket> packet;
        if (Status status = detail::cookNodePacket(source, packet); !status.ok) return status;

        std::set<std::string> names;
        for (const auto& primitive : packet->getPrimitives())
        {
            if (!primitive) continue;
            for (const attr::AttributeOwner owner : owners)
            {
                // Only a mesh has vertices and faces.
                const bool meshOnly = owner == attr::AttributeOwner::VERTEX || owner == attr::AttributeOwner::FACE;
                if (meshOnly && !dynamic_cast<const geo::Mesh*>(primitive.get())) continue;
                const auto count = static_cast<unsigned int>(primitive->getNumAttributes(owner));
                for (unsigned int index = 0; index < count; ++index)
                {
                    const auto attribute = primitive->getAttributeByIndex(owner, index).lock();
                    if (!attribute || attribute->isInternal()) continue;
                    if (std::find(types.begin(), types.end(), attribute->getType()) == types.end()) continue;
                    names.insert(attribute->getName());
                }
            }
        }
        result.names.assign(names.begin(), names.end());
        return success();
    });
    return result;
}

} // namespace helix::core
