#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include "Engine/Core/Path.h"
#include "Engine/Daslang/Runtime.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Serializer/Serializer.h"
#include "Graphics/DisplayGeometry.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

#ifndef HELIX_VERSION
#define HELIX_VERSION "unknown"
#endif

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

// Everything here runs on the one thread the header names, so plain variables do.
bool initialized = false;
bool sceneModified = false;
std::string importSummary;

Status requireInitialized()
{
    return initialized ? success() : failure("The engine is not initialized.");
}

Status requireNode(NodeId node)
{
    if (!nt::nm().isValidNode(node)) return failure("There is no node " + std::to_string(node) + ".");
    return success();
}

// Returns the parameter of a node, or a failed status saying why not.
Status findParameter(NodeId node, const std::string& name, std::shared_ptr<prm::NodeParameter>& out)
{
    if (Status status = requireInitialized(); !status.ok) return status;
    if (Status status = requireNode(node); !status.ok) return status;

    out = nt::nm().getNode(node).getParameter(name).lock();
    if (!out) return failure("The node has no parameter named '" + name + "'.");
    return success();
}

Status checkComponent(const prm::Parameter& parameter, std::uint32_t component)
{
    if (component >= parameter.getVectorSize())
        return failure(
            "Component " + std::to_string(component) + " is out of range, the parameter holds " +
            std::to_string(parameter.getVectorSize()) + "."
        );
    return success();
}

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

bool isIntegerType(prm::Type type)
{
    return type == prm::Type::INT || type == prm::Type::BOOL || type == prm::Type::TOGGLE;
}

bool isStringType(prm::Type type)
{
    return type == prm::Type::STRING || type == prm::Type::DROPDOWN;
}

Status wrongType(const std::string& name, const char* wanted)
{
    return failure("The parameter '" + name + "' is not " + wanted + ".");
}

NodeInfo describe(nt::NodeId id, nt::Node& node)
{
    NodeInfo info;
    info.id = id;
    info.type = node.getType().getFullName();
    info.name = node.getName();
    info.path = node.getPath().getString();
    info.x = node.getPosition().x();
    info.y = node.getPosition().y();
    return info;
}

// Returns whether a node can be reached by following outputs from another.
bool reaches(nt::NodeId from, nt::NodeId target)
{
    std::unordered_set<nt::NodeId> visited;
    std::vector<nt::NodeId> pending{from};
    while (!pending.empty())
    {
        const nt::NodeId current = pending.back();
        pending.pop_back();
        if (current == target) return true;
        if (!visited.insert(current).second) continue;
        for (const nt::NodeLink& link : nt::nm().graph().getOutputs(current))
            pending.push_back(link.targetNode);
    }
    return false;
}

Status checkPorts(
    NodeId sourceNode,
    std::uint32_t sourceOutput,
    NodeId targetNode,
    std::uint32_t targetInput
)
{
    if (Status status = requireInitialized(); !status.ok) return status;
    if (Status status = requireNode(sourceNode); !status.ok) return status;
    if (Status status = requireNode(targetNode); !status.ok) return status;

    nt::Node& source = nt::nm().getNode(sourceNode);
    nt::Node& target = nt::nm().getNode(targetNode);
    if (sourceOutput >= source.getMaxOutputs())
        return failure("The node " + source.getName() + " has no output " + std::to_string(sourceOutput) + ".");
    if (!target.getType().getPortAt(targetInput))
        return failure("The node " + target.getName() + " has no input " + std::to_string(targetInput) + ".");
    return success();
}

void markModified() { sceneModified = true; }

// The facade cooks on the calling thread. Background cooking is off unless something turned it
// on, and when it was on, a cook still running has to finish before the thread cooks too, since
// two cooks over one network race.
void ensureSynchronousCooking()
{
    nt::NetworkManager& manager = nt::nm();
    if (!manager.isAsyncCookingEnabled() && !manager.isCookBusy()) return;

    manager.setAsyncCookingEnabled(false);
    manager.cancelCook();
    for (int attempt = 0; attempt < 5000 && (manager.isCookBusy() || manager.isCooking()); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// Cooks a node and returns what its first output produced.
Status cookNode(NodeId node, std::shared_ptr<const NodePacket>& packet)
{
    if (Status status = requireInitialized(); !status.ok) return status;
    if (Status status = requireNode(node); !status.ok) return status;
    ensureSynchronousCooking();

    nt::nm().cook(node);
    packet = nt::nm().getNode(node).getOutputPacket(0);
    if (!packet) return failure("The node produced no geometry.");
    return success();
}

// Cooks the display node and returns what it produced.
Status cookDisplay(std::shared_ptr<const NodePacket>& packet)
{
    if (Status status = requireInitialized(); !status.ok) return status;

    const std::optional<nt::NodeId> display = nt::nm().getDisplayNode();
    if (!display || !nt::nm().isValidNode(*display)) return failure("No node is displayed.");
    Status status = cookNode(*display, packet);
    if (!status.ok && status.error == "The node produced no geometry.")
        return failure("The display node produced no geometry.");
    return status;
}

void appendVectors(std::vector<float>& out, const std::vector<glm::vec3>& vectors)
{
    out.reserve(vectors.size() * 3);
    for (const glm::vec3& vector : vectors)
    {
        out.push_back(vector.x);
        out.push_back(vector.y);
        out.push_back(vector.z);
    }
}

// Writes the scene next to its destination, then renames it over the destination.
Status writeAtomically(const std::filesystem::path& destination)
{
    namespace fs = std::filesystem;

    const fs::path directory =
        destination.has_parent_path() ? destination.parent_path() : fs::path(".");
    std::error_code ignored;
    if (!fs::is_directory(directory, ignored))
        return failure("The folder " + directory.string() + " does not exist.");

    // The temporary file sits in the destination's folder, since a rename is only atomic
    // inside one volume.
    static std::atomic<unsigned> counter{0};
    const fs::path temporary =
        directory / (destination.filename().string() + ".tmp" + std::to_string(counter++));

    try
    {
        nt::Serializer().save(nt::nm(), temporary.string());
        fs::rename(temporary, destination);
    }
    catch (const std::exception& error)
    {
        fs::remove(temporary, ignored);
        return failure(std::string("Couldn't save the scene: ") + error.what());
    }
    return success();
}

// What the facade tells about a type, from the type itself.
NodeTypeInfo describeType(
    const std::string& name,
    const std::string& label,
    const std::vector<std::string>& tags,
    const nt::NodeType& type
)
{
    NodeTypeInfo info;
    info.name = name;
    info.label = label;
    info.tags = tags;
    info.outputCount = type.maxOutputs;
    for (const nt::InputPort& port : type.inputPorts)
        info.inputs.push_back({port.label, port.multiInput, port.optional});
    return info;
}

// Describes a type or an alias by its full name, or returns false when there is none.
bool describeTypeNamed(const std::string& fullName, NodeTypeInfo& info)
{
    if (const nt::NodeType* type = nt::NodeTypeTable::getNodeType(fullName))
    {
        info = describeType(type->getFullName(), type->getLabel(), type->tags, *type);
        return true;
    }
    if (const nt::NodeAlias* alias = nt::NodeTypeTable::getNodeAlias(fullName))
    {
        const nt::NodeType* aliased = nt::NodeTypeTable::getNodeType(alias->aliasedType);
        if (!aliased) return false;
        info = describeType(alias->getFullName(), alias->getLabel(), alias->tags, *aliased);
        return true;
    }
    return false;
}

// The engine's own undo stack records every edit made through the facade, and nothing reads it:
// the interface undoes with scene snapshots. Left alone it would keep a copy of everything a
// person deleted for as long as the scene is open, so every edit empties it.
void dropEngineUndo() { nt::nm().undoStack().clear(); }

} // namespace

namespace detail {

Status cookDisplayPacket(std::shared_ptr<const NodePacket>& packet) { return cookDisplay(packet); }

Status cookNodePacket(NodeId node, std::shared_ptr<const NodePacket>& packet)
{
    return cookNode(node, packet);
}

} // namespace detail

Status initialize(const InitOptions& options)
{
    if (initialized) return success();

    return guarded([&] {
        if (!options.runtimeDirectory.empty())
            daslang::setStandardLibraryRoot(options.runtimeDirectory);
        nt::NodeLoader::setDirectories(options.nodesDirectory, options.librariesDirectory);
        nt::NodeLoader::loadNodes();

        // Anything that edits the graph marks the scene as changed. Loading and clearing
        // fire the same signals, so those entry points reset the flag once they are done.
        nt::NetworkManager& manager = nt::nm();
        manager.nodeCreated.connect([](nt::NodeId) { markModified(); });
        manager.nodeRemoved.connect([](nt::NodeId) { markModified(); });
        manager.nodeLinkCreated.connect([](nt::NodeLink) { markModified(); });
        manager.nodeLinkRemoved.connect([](nt::NodeLink) { markModified(); });
        manager.nodePositionChanged.connect([](nt::NodeId, Vector2) { markModified(); });

        initialized = true;
        sceneModified = false;
        return success();
    });
}

bool isInitialized() { return initialized; }

std::string version() { return HELIX_VERSION; }

Status newScene()
{
    return guarded([] {
        if (Status status = requireInitialized(); !status.ok) return status;
        ensureSynchronousCooking();
        nt::nm().clear();
        sceneModified = false;
        return success();
    });
}

Status openScene(const std::string& path)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;

        std::error_code ignored;
        if (!std::filesystem::is_regular_file(path, ignored))
            return failure("There is no scene file at " + path + ".");

        ensureSynchronousCooking();
        try
        {
            nt::Serializer().load(nt::nm(), path);
        }
        catch (const std::exception& error)
        {
            // The engine empties the scene before it reads, so what is left is not the
            // scene that was open, and not the file's either.
            nt::nm().clear();
            sceneModified = false;
            return failure("Couldn't read " + path + ": " + error.what());
        }
        sceneModified = false;
        return success();
    });
}

Status saveScene(const std::string& path)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (path.empty()) return failure("No file name was given.");

        Status status = writeAtomically(std::filesystem::path(path));
        if (status.ok) sceneModified = false;
        return status;
    });
}

bool isSceneModified() { return initialized && sceneModified; }

SceneSnapshot captureScene()
{
    SceneSnapshot result;
    result.status = guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        std::ostringstream stream;
        nt::Serializer().save(nt::nm(), stream);
        result.data = stream.str();
        return success();
    });
    return result;
}

Status restoreScene(const std::string& data)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;

        // Loading fires the signals that mark the scene as changed, and a restore is not a
        // judgement on whether the file on disk matches, so the flag is put back as it was.
        const bool wasModified = sceneModified;
        ensureSynchronousCooking();
        try
        {
            std::istringstream stream(data);
            nt::Serializer().load(nt::nm(), stream);
        }
        catch (const std::exception& error)
        {
            nt::nm().clear();
            sceneModified = wasModified;
            return failure(std::string("Couldn't restore the scene: ") + error.what());
        }
        sceneModified = wasModified;
        return success();
    });
}

std::vector<NodeInfo> listNodes()
{
    std::vector<NodeInfo> nodes;
    if (!initialized) return nodes;
    for (auto [id, node] : nt::nm().nodes())
        nodes.push_back(describe(id, node));
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo& a, const NodeInfo& b) {
        return a.id < b.id;
    });
    return nodes;
}

std::vector<LinkInfo> listLinks()
{
    std::vector<LinkInfo> links;
    if (!initialized) return links;
    for (auto [id, node] : nt::nm().nodes())
    {
        for (const nt::NodeLink& link : nt::nm().graph().getOutputs(id))
            links.push_back({link.sourceNode, link.sourceOutput, link.targetNode, link.targetInput});
    }
    std::sort(links.begin(), links.end(), [](const LinkInfo& a, const LinkInfo& b) {
        return std::tie(a.sourceNode, a.sourceOutput, a.targetNode, a.targetInput) <
               std::tie(b.sourceNode, b.sourceOutput, b.targetNode, b.targetInput);
    });
    return links;
}

NodeTypeInfoResult nodeTypeInfo(const std::string& type)
{
    NodeTypeInfoResult result;
    result.status = guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (!describeTypeNamed(type, result.info)) return failure("There is no node type '" + type + "'.");
        return success();
    });
    return result;
}

std::vector<NodeTypeInfo> listNodeTypeInfos()
{
    std::vector<NodeTypeInfo> infos;
    for (const std::string& name : listNodeTypes())
    {
        NodeTypeInfo info;
        if (describeTypeNamed(name, info)) infos.push_back(std::move(info));
    }
    return infos;
}

std::vector<std::string> listNodeTypes()
{
    std::vector<std::string> types;
    if (!initialized) return types;
    for (const nt::NodeType& type : nt::NodeTypeTable::getData())
        types.push_back(type.getFullName());
    for (const nt::NodeAlias& alias : nt::NodeTypeTable::getNodeAliases())
        types.push_back(alias.typeNamespace + "::" + alias.internalName);
    std::sort(types.begin(), types.end());
    return types;
}

CreateNodeResult createNode(const std::string& type, const std::string& name, float x, float y)
{
    CreateNodeResult result;
    result.status = guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;

        if (!nt::NodeTypeTable::getNodeAlias(type) && !nt::NodeTypeTable::getNodeType(type))
            return failure("There is no node type '" + type + "'.");

        // A name the caller gives is used as it is or refused, never numbered into a
        // different one. Only a node left unnamed gets the engine's free name.
        if (!name.empty())
        {
            if (!Path::isValidName(name))
                return failure("'" + name + "' cannot name a node, use letters, digits and underscores.");
            if (nt::nm().getNodeAtPath(Path("/" + name)))
                return failure("A node named '" + name + "' already exists.");
        }

        result.id = nt::nm().createNode(type, Path("/"), name, Vector2(x, y));
        dropEngineUndo();
        return success();
    });
    return result;
}

Status deleteNode(NodeId node)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (Status status = requireNode(node); !status.ok) return status;
        nt::nm().deleteNode(node);
        dropEngineUndo();
        return success();
    });
}

Status connectNodes(
    NodeId sourceNode,
    std::uint32_t sourceOutput,
    NodeId targetNode,
    std::uint32_t targetInput
)
{
    return guarded([&] {
        if (Status status = checkPorts(sourceNode, sourceOutput, targetNode, targetInput); !status.ok)
            return status;

        // The link would close a loop when the target already leads back to the source.
        if (reaches(targetNode, sourceNode))
            return failure("The link would make the node depend on itself.");

        nt::nm().connectNodes(sourceNode, sourceOutput, targetNode, targetInput);
        dropEngineUndo();
        return success();
    });
}

Status disconnectNodes(
    NodeId sourceNode,
    std::uint32_t sourceOutput,
    NodeId targetNode,
    std::uint32_t targetInput
)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;

        const nt::NodeLink link{sourceNode, sourceOutput, targetNode, targetInput};
        const std::vector<nt::NodeLink> existing = nt::nm().graph().getOutputs(sourceNode);
        if (std::find(existing.begin(), existing.end(), link) == existing.end())
            return failure("Those two ports are not linked.");

        nt::nm().disconnectNodes(link);
        dropEngineUndo();
        return success();
    });
}

Status moveNode(NodeId node, float x, float y)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (Status status = requireNode(node); !status.ok) return status;
        nt::nm().moveNode(node, Vector2(x, y));
        dropEngineUndo();
        return success();
    });
}

Status setDisplayNode(NodeId node)
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (Status status = requireNode(node); !status.ok) return status;
        ensureSynchronousCooking();
        nt::nm().setDisplayNode(node);
        return success();
    });
}

Status clearDisplayNode()
{
    return guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        ensureSynchronousCooking();
        if (nt::nm().getDisplayNode()) nt::nm().clearDisplayFlag();
        return success();
    });
}

NodeId displayNode()
{
    if (!initialized) return 0;
    const std::optional<nt::NodeId> display = nt::nm().getDisplayNode();
    return display && nt::nm().isValidNode(*display) ? *display : 0;
}

std::vector<std::string> supportedImportExtensions() { return import::supportedExtensions(); }

Status setCacheDirectory(const std::string& directory)
{
    return guarded([&] {
        if (directory.empty()) return failure("No folder was given.");
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error || !std::filesystem::is_directory(directory, error))
            return failure("Couldn't use " + directory + " as the cache folder.");
        import::setBlendCacheDirectory(directory);
        return success();
    });
}

CreateNodeResult importFile(const std::string& path)
{
    CreateNodeResult result;
    result.status = guarded([&] {
        if (Status status = requireInitialized(); !status.ok) return status;
        if (path.empty()) return failure("No file was given.");

        const std::filesystem::path file =
            std::filesystem::absolute(std::filesystem::path(path)).lexically_normal();

        // The default settings the new node has, so the cook of the node finds the result of
        // this read in the cache.
        const import::ImportSummary summary =
            import::importFile(file, import::ImportOptions{})->summary;

        // The node is named after the file when that name is free and valid. Otherwise the
        // engine picks a free one.
        std::string name = file.stem().string();
        for (char& c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) c = '_';
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) ||
            !Path::isValidName(name) || nt::nm().getNodeAtPath(Path("/" + name)))
            name.clear();

        const float y = 140.f * static_cast<float>(listNodes().size());
        result.id = nt::nm().createNode("helix::geometryImport", Path("/"), name, Vector2(0.f, y));

        const std::shared_ptr<prm::NodeParameter> filePath =
            nt::nm().getNode(result.id).getParameter("filePath").lock();
        if (!filePath)
        {
            nt::nm().deleteNode(result.id);
            result.id = 0;
            return failure("The import node has no file path parameter.");
        }
        filePath->setString(file.string());
        dropEngineUndo();
        importSummary = summary.text();
        return success();
    });
    return result;
}

std::string lastImportSummary() { return importSummary; }

std::vector<ParameterInfo> listParameters(NodeId node)
{
    std::vector<ParameterInfo> parameters;
    if (!initialized || !nt::nm().isValidNode(node)) return parameters;

    for (const std::weak_ptr<prm::NodeParameter>& weak : nt::nm().getNode(node).getParameters())
    {
        const std::shared_ptr<prm::NodeParameter> parameter = weak.lock();
        if (!parameter) continue;
        parameters.push_back(
            {parameter->getName(),
             parameter->getLabel(),
             toParameterType(parameter->getType()),
             parameter->getVectorSize()}
        );
    }
    return parameters;
}

FloatValue getFloat(NodeId node, const std::string& parameter, std::uint32_t component)
{
    FloatValue result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::FLOAT) return wrongType(parameter, "a float");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        result.value = found->evalFloat(component);
        return success();
    });
    return result;
}

IntValue getInt(NodeId node, const std::string& parameter, std::uint32_t component)
{
    IntValue result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (!isIntegerType(found->getType())) return wrongType(parameter, "an integer or a toggle");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        result.value = found->evalInt(component);
        return success();
    });
    return result;
}

StringValue getString(NodeId node, const std::string& parameter, std::uint32_t component)
{
    StringValue result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (!isStringType(found->getType())) return wrongType(parameter, "a string");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        result.value = found->evalString(component);
        return success();
    });
    return result;
}

Vector3Value getVector3(NodeId node, const std::string& parameter)
{
    Vector3Value result;
    result.status = guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::FLOAT || found->getVectorSize() != 3)
            return wrongType(parameter, "a vector of three floats");
        result.x = found->evalFloat(0);
        result.y = found->evalFloat(1);
        result.z = found->evalFloat(2);
        return success();
    });
    return result;
}

Status setFloat(NodeId node, const std::string& parameter, std::uint32_t component, float value)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::FLOAT) return wrongType(parameter, "a float");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        found->setFloat(value, component);
        markModified();
        return success();
    });
}

Status setInt(NodeId node, const std::string& parameter, std::uint32_t component, std::int64_t value)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (!isIntegerType(found->getType())) return wrongType(parameter, "an integer or a toggle");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        found->setInt(static_cast<intT>(value), component);
        markModified();
        return success();
    });
}

Status
setString(NodeId node, const std::string& parameter, std::uint32_t component, const std::string& value)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (!isStringType(found->getType())) return wrongType(parameter, "a string");
        if (Status status = checkComponent(*found, component); !status.ok) return status;
        found->setString(value, component);
        markModified();
        return success();
    });
}

Status setVector3(NodeId node, const std::string& parameter, float x, float y, float z)
{
    return guarded([&] {
        std::shared_ptr<prm::NodeParameter> found;
        if (Status status = findParameter(node, parameter, found); !status.ok) return status;
        if (found->getType() != prm::Type::FLOAT || found->getVectorSize() != 3)
            return wrongType(parameter, "a vector of three floats");
        found->setFloat(x, 0);
        found->setFloat(y, 1);
        found->setFloat(z, 2);
        markModified();
        return success();
    });
}

TimeInfo timeInfo()
{
    TimeInfo info;
    if (!initialized) return info;
    const nt::NetworkManager& manager = nt::nm();
    info.frame = manager.getFrame();
    info.startFrame = static_cast<std::int32_t>(manager.getStartFrame());
    info.endFrame = static_cast<std::int32_t>(manager.getEndFrame());
    info.fps = manager.getFps();
    return info;
}

void setFrame(float frame)
{
    if (!initialized) return;
    nt::nm().setFrame(frame);
}

GeometrySummary cookDisplaySummary()
{
    GeometrySummary summary;
    summary.status = guarded([&] {
        std::shared_ptr<const NodePacket> packet;
        if (Status status = cookDisplay(packet); !status.ok) return status;

        for (const auto& primitive : packet->getPrimitives())
        {
            if (!primitive) continue;
            ++summary.primitiveCount;
            summary.pointCount += primitive->getNumPoints();
            if (const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(primitive))
                summary.faceCount += mesh->getNumFaces();
        }

        gfx::DisplayGeometry geometry;
        gfx::buildDisplayGeometry(geometry, *packet);
        summary.triangleCount = geometry.topology.triangleIndices.size() / 3;
        return success();
    });
    return summary;
}

DisplayBuffers cookDisplayBuffers()
{
    DisplayBuffers buffers;
    buffers.status = guarded([&] {
        std::shared_ptr<const NodePacket> packet;
        if (Status status = cookDisplay(packet); !status.ok) return status;

        gfx::DisplayGeometry geometry;
        gfx::buildDisplayGeometry(geometry, *packet);

        appendVectors(buffers.positions, geometry.positions);
        appendVectors(buffers.normals, geometry.normals);
        appendVectors(buffers.pointPositions, geometry.pointPositions);
        buffers.triangleIndices = std::move(geometry.topology.triangleIndices);
        buffers.edgeIndices = std::move(geometry.topology.edgeIndices);
        buffers.lineIndices = std::move(geometry.topology.lineIndices);
        return success();
    });
    return buffers;
}

} // namespace helix::core
