// The editing half of the facade that works on whole nodes: renaming, and copying and pasting a
// group of them. Creating, deleting and wiring are in HelixCore.cpp. See the Graph section of
// HelixCore.h.

#include "Engine/Core/Path.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeReferences.h"
#include "Engine/Network/NodeSnapshot.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <algorithm>
#include <cctype>
#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

// A copy of a few hundred nodes is a few megabytes of text. A pasted text far beyond that is not
// one, and is refused before it is read.
constexpr std::size_t maxCopyTextBytes = 32u * 1024u * 1024u;

constexpr const char* clipFormat = "helix.nodes";
constexpr int clipVersion = 1;

// One link of a copy, between two nodes of the copy by their place in it.
struct ClipLink
{
    unsigned int source = 0;
    unsigned int sourceOutput = 0;
    unsigned int target = 0;
    unsigned int targetInput = 0;

    template <class Archive> void serialize(Archive& archive)
    {
        archive(
            cereal::make_nvp("source", source),
            cereal::make_nvp("sourceOutput", sourceOutput),
            cereal::make_nvp("target", target),
            cereal::make_nvp("targetInput", targetInput)
        );
    }
};

// The keyframes of one value of one node of the copy.
struct ClipTrack
{
    unsigned int node = 0;
    std::string parameter;
    unsigned int component = 0;
    float baseValue = 0.f;
    bool hasBaseExpression = false;
    std::string baseExpression;
    std::vector<sim::Keyframe> keyframes;

    template <class Archive> void serialize(Archive& archive)
    {
        archive(
            cereal::make_nvp("node", node),
            cereal::make_nvp("parameter", parameter),
            cereal::make_nvp("component", component),
            cereal::make_nvp("baseValue", baseValue),
            cereal::make_nvp("hasBaseExpression", hasBaseExpression),
            cereal::make_nvp("baseExpression", baseExpression),
            cereal::make_nvp("keyframes", keyframes)
        );
    }
};

struct Clip
{
    std::string format = clipFormat;
    int version = clipVersion;
    std::vector<nt::NodeSnapshot> nodes;
    std::vector<ClipLink> links;
    std::vector<ClipTrack> tracks;

    template <class Archive> void save(Archive& archive) const
    {
        archive(
            cereal::make_nvp("format", format),
            cereal::make_nvp("version", version),
            cereal::make_nvp("nodes", nodes),
            cereal::make_nvp("links", links),
            cereal::make_nvp("tracks", tracks)
        );
    }

    // The tag and the version are read first, so text that is not a copy is turned down before
    // anything of it is read as one.
    template <class Archive> void load(Archive& archive)
    {
        archive(cereal::make_nvp("format", format), cereal::make_nvp("version", version));
        if (format != clipFormat) throw NotACopy("The text is not a copy of Helix nodes.");
        if (version > clipVersion || version < 1)
            throw NotACopy("The copy was made by another version of Helix and cannot be pasted here.");
        archive(
            cereal::make_nvp("nodes", nodes),
            cereal::make_nvp("links", links),
            cereal::make_nvp("tracks", tracks)
        );
    }

    struct NotACopy : cereal::Exception
    {
        using cereal::Exception::Exception;
    };
};

// A name to give a node whose wanted name is taken: the next number up, as Path::increment
// numbers. Written out so that a huge number in a name that came from text cannot overflow.
std::string numbered(const std::string& wanted, const std::function<bool(const std::string&)>& taken)
{
    if (!taken(wanted)) return wanted;

    std::size_t digits = wanted.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(wanted[digits - 1]))) --digits;
    const std::string base = wanted.substr(0, digits);
    const std::string suffix = wanted.substr(digits);
    long number = (suffix.empty() || suffix.size() > 9) ? 0 : std::stol(suffix);

    for (;;)
    {
        const std::string candidate = base + std::to_string(++number);
        if (!taken(candidate)) return candidate;
    }
}

bool nameTakenInScene(const std::string& name)
{
    return nt::nm().getNodeAtPath(Path("/" + name)) != nullptr;
}

bool isTopLevel(const Path& path) { return path.getParent().isRoot(); }

// Whether the links among the nodes of a copy make a loop. A loop would be refused one link at
// a time when they are made, after the nodes already exist.
bool linksLoop(std::size_t nodeCount, const std::vector<ClipLink>& links)
{
    std::vector<unsigned int> incoming(nodeCount, 0);
    std::vector<std::vector<unsigned int>> outgoing(nodeCount);
    for (const ClipLink& link : links)
    {
        outgoing[link.source].push_back(link.target);
        ++incoming[link.target];
    }
    std::vector<unsigned int> ready;
    for (unsigned int node = 0; node < nodeCount; ++node)
        if (incoming[node] == 0) ready.push_back(node);
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const unsigned int node = ready.back();
        ready.pop_back();
        ++visited;
        for (unsigned int target : outgoing[node])
            if (--incoming[target] == 0) ready.push_back(target);
    }
    return visited != nodeCount;
}

// Says what is wrong with a copy, or nothing when it can be pasted.
std::string problemWith(const Clip& clip)
{
    if (clip.nodes.empty()) return "The copy holds no node.";
    if (clip.nodes.size() > maxCopiedNodes) return "The copy holds too many nodes to paste.";

    std::vector<const nt::NodeType*> types;
    for (const nt::NodeSnapshot& snapshot : clip.nodes)
    {
        const nt::NodeType* type = nt::NodeTypeTable::getNodeType(snapshot.getTypeName());
        if (!type) return "There is no node type '" + snapshot.getTypeName() + "'.";
        if (type->hasChildScope()) return "A node that holds other nodes cannot be pasted.";
        if (!isTopLevel(snapshot.getPath())) return "Only the nodes at the top of a scene can be pasted.";
        if (!std::isfinite(snapshot.getPosition().x()) || !std::isfinite(snapshot.getPosition().y()))
            return "A node of the copy has no position.";
        types.push_back(type);
    }

    std::set<std::pair<unsigned int, unsigned int>> wired;
    for (const ClipLink& link : clip.links)
    {
        if (link.source >= clip.nodes.size() || link.target >= clip.nodes.size() || link.source == link.target)
            return "A link of the copy joins nodes that are not in it.";
        if (link.sourceOutput >= types[link.source]->maxOutputs)
            return "A link of the copy leaves an output that does not exist.";
        const nt::InputPort* port = types[link.target]->getPortAt(link.targetInput);
        if (!port) return "A link of the copy enters an input that does not exist.";
        // Several links can enter a multi input, each on a place of its own, and only there.
        if (!wired.insert({link.target, link.targetInput}).second)
            return "Two links of the copy enter the same input.";
    }
    if (linksLoop(clip.nodes.size(), clip.links)) return "The links of the copy make a loop.";

    for (const ClipTrack& track : clip.tracks)
    {
        if (track.node >= clip.nodes.size()) return "A keyframe of the copy belongs to no node of it.";
        for (const sim::Keyframe& key : track.keyframes)
        {
            if (!std::isfinite(key.frame) || !std::isfinite(key.value))
                return "A keyframe of the copy has no value.";
            if (static_cast<int>(key.interpolation) < 0 || static_cast<int>(key.interpolation) > 2)
                return "A keyframe of the copy has an unknown interpolation.";
        }
    }
    return {};
}

} // namespace

std::string freeNodeName(const std::string& wanted)
{
    if (!isInitialized()) return wanted;
    return numbered(wanted, nameTakenInScene);
}

Status renameNode(NodeId node, const std::string& newName)
{
    return guarded([&] {
        if (Status status = detail::requireNodeInScene(node); !status.ok) return status;

        nt::Node& target = nt::nm().getNode(node);
        if (target.getName() == newName) return success();

        // The name is not copied into a message when it is long: it came from a text field or a
        // script and can be as large as either.
        if (newName.size() > maxNodeNameLength)
            return failure("A name has at most " + std::to_string(maxNodeNameLength) + " characters.");
        if (!Path::isValidName(newName))
            return failure("'" + newName + "' cannot name a node, use letters, digits and underscores.");
        if (nt::nm().getNodeAtPath(target.getPath().getParent().append(Path(newName))))
            return failure("A node named '" + newName + "' already exists.");
        if (nt::nm().getScope(target.getPath()))
            return failure("A node that holds other nodes cannot be renamed.");

        detail::ensureSynchronousCooking();
        nt::nm().renameNode(node, newName);
        detail::dropEngineUndo();
        detail::noteSceneModified();
        return success();
    });
}

CopyNodesResult copyNodes(const std::vector<NodeId>& nodes)
{
    CopyNodesResult result;
    result.status = guarded([&] {
        if (!isInitialized()) return failure("The engine is not initialized.");
        if (nodes.empty()) return failure("There are no nodes to copy.");
        if (nodes.size() > maxCopiedNodes) return failure("There are too many nodes to copy at once.");

        // The copy lists the nodes in the order they were made, whatever order they were asked in.
        const std::set<NodeId> ordered(nodes.begin(), nodes.end());
        Clip clip;
        std::unordered_map<NodeId, unsigned int> indexOf;
        for (const NodeId id : ordered)
        {
            if (Status status = detail::requireNodeInScene(id); !status.ok) return status;
            nt::Node& node = nt::nm().getNode(id);
            if (node.getType().hasChildScope() || nt::nm().getScope(node.getPath()))
                return failure("A node that holds other nodes cannot be copied.");
            if (!isTopLevel(node.getPath()))
                return failure("Only the nodes at the top of a scene can be copied.");
            indexOf[id] = static_cast<unsigned int>(clip.nodes.size());
            clip.nodes.push_back(nt::NodeSnapshot::capture(node));
        }

        // Only the links between copied nodes: a link to a node left behind would have nothing
        // to join to once pasted.
        for (const NodeId id : ordered)
        {
            for (const nt::NodeLink& link : nt::nm().graph().getOutputs(id))
            {
                const auto target = indexOf.find(link.targetNode);
                if (target == indexOf.end()) continue;
                clip.links.push_back({indexOf[id], link.sourceOutput, target->second, link.targetInput});
            }
        }

        sim::KeyframeStore& store = sim::KeyframeStore::instance();
        for (const NodeId id : ordered)
        {
            for (const sim::KeyframeTarget& target : store.getTargets(id))
            {
                const std::optional<sim::KeyframeStore::TrackCopy> track = store.copyTrack(target);
                if (!track) continue;
                ClipTrack clipTrack;
                clipTrack.node = indexOf[id];
                clipTrack.parameter = target.parameter;
                clipTrack.component = target.component;
                clipTrack.baseValue = track->baseValue;
                clipTrack.hasBaseExpression = track->baseExpression.has_value();
                clipTrack.baseExpression = track->baseExpression.value_or("");
                clipTrack.keyframes = track->keyframes;
                clip.tracks.push_back(std::move(clipTrack));
            }
        }

        std::ostringstream stream;
        {
            // No indentation, so the text on a clipboard is as short as the data allows.
            cereal::JSONOutputArchive::Options options(
                std::numeric_limits<double>::max_digits10, cereal::JSONOutputArchive::Options::IndentChar::space, 0
            );
            cereal::JSONOutputArchive archive(stream, options);
            archive(cereal::make_nvp("helixNodes", clip));
        }
        result.text = stream.str();
        return success();
    });
    return result;
}

PasteNodesResult pasteNodes(const std::string& text, float offsetX, float offsetY)
{
    PasteNodesResult result;
    result.status = guarded([&] {
        if (!isInitialized()) return failure("The engine is not initialized.");
        if (!std::isfinite(offsetX) || !std::isfinite(offsetY)) return failure("The offset is not a number.");
        if (text.size() > maxCopyTextBytes) return failure("The text is too large to be a copy of nodes.");

        Clip clip;
        try
        {
            std::istringstream stream(text);
            cereal::JSONInputArchive archive(stream);
            archive(cereal::make_nvp("helixNodes", clip));
        }
        catch (const Clip::NotACopy& error)
        {
            return failure(error.what());
        }
        catch (const std::exception&)
        {
            // What the parser says quotes the text, which can be anything, so it is not passed on.
            return failure("The text is not a copy of Helix nodes.");
        }
        if (const std::string problem = problemWith(clip); !problem.empty()) return failure(problem);

        // The names come first, so that nothing is made when one cannot be found.
        std::unordered_set<std::string> chosen;
        std::vector<std::string> names;
        nt::NodeRenames renames;
        for (nt::NodeSnapshot& snapshot : clip.nodes)
        {
            std::string wanted = snapshot.getPath().getName();
            if (!Path::isValidName(wanted) || wanted.size() > maxNodeNameLength)
                wanted = nt::NodeTypeTable::requireNodeType(snapshot.getTypeName()).internalName + "1";
            const std::string name = numbered(wanted, [&](const std::string& candidate) {
                return nameTakenInScene(candidate) || chosen.count(candidate) > 0;
            });
            chosen.insert(name);
            names.push_back(name);
            const std::string before = snapshot.getPath().getString();
            const std::string after = "/" + name;
            if (before != after) renames[before] = after;
        }

        // An expression that read a copied node reads its copy. This is done on the text of the
        // copy, before any node exists, so no expression ever points at a node that is not there.
        for (nt::NodeSnapshot& snapshot : clip.nodes)
            for (ParameterSerializable& parameter : snapshot.parameters())
                nt::rewriteNodeReferences(parameter, Path("/"), renames);

        detail::ensureSynchronousCooking();
        nt::NetworkManager& manager = nt::nm();
        std::vector<NodeId> created;
        try
        {
            for (std::size_t index = 0; index < clip.nodes.size(); ++index)
            {
                const nt::NodeSnapshot& snapshot = clip.nodes[index];
                const NodeId id = manager.network().reserveNodeId();
                const Vector2 position(snapshot.getPosition().x() + offsetX, snapshot.getPosition().y() + offsetY);
                snapshot.restore(id, Path("/" + names[index]), position);
                created.push_back(id);
            }

            for (const ClipLink& link : clip.links)
                manager.connectNodes(created[link.source], link.sourceOutput, created[link.target], link.targetInput);

            // A keyframe of a parameter this build does not have is left out: the copy was made
            // here, so it only happens with text written by hand.
            for (const ClipTrack& track : clip.tracks)
            {
                nt::Node& node = manager.getNode(created[track.node]);
                const auto parameter = node.getParameter(track.parameter).lock();
                if (!parameter || parameter->getValueType() != prm::ValueType::Float) continue;
                if (track.component >= parameter->getVectorSize()) continue;
                sim::KeyframeStore::TrackCopy copy;
                copy.baseValue = track.baseValue;
                if (track.hasBaseExpression) copy.baseExpression = track.baseExpression;
                copy.keyframes = track.keyframes;
                sim::KeyframeStore::instance().pasteTrack(
                    sim::KeyframeTarget{created[track.node], track.parameter, track.component}, copy
                );
            }
        }
        catch (...)
        {
            // Nothing is left half pasted.
            for (const NodeId id : created)
                if (manager.isValidNode(id)) manager.deleteNode(id);
            detail::dropEngineUndo();
            throw;
        }

        detail::dropEngineUndo();
        detail::noteSceneModified();
        result.nodes = created;
        result.names = names;
        return success();
    });
    return result;
}

} // namespace helix::core
