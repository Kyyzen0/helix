#pragma once

/**
 * @file HelixCore.h
 * @brief The facade a native application drives the engine through.
 *
 * The engine is a C++ library whose headers pull in Eigen, Boost, TBB, glm, cereal and
 * daslang. A Swift application cannot, and should not have to, import all of that, so this
 * header is the one it imports instead. It holds nothing but the standard library and plain
 * structs, and the functions behind it do the translation.
 *
 * @par Errors
 * No exception crosses this boundary. A function that can fail returns a helix::core::Status,
 * or a struct that starts with one, and `ok` tells whether it worked. Read the other fields of
 * a result only when `ok` is true. `error` is a short sentence meant to be shown to a person.
 * A function that only asks a question (a count, a list) cannot fail and returns its answer
 * directly, an empty one when there is nothing to report.
 *
 * @par Threads
 * The engine is one network behind a singleton, and it is not thread safe. Every function in
 * this header has to be called from the same thread, which is normally the main thread of the
 * application, and two calls must never overlap. The facade takes no lock of its own.
 *
 * What the engine does on its own threads is invisible from here. NetworkManager can cook in
 * the background on a CookScheduler pool: the pool works from a snapshot taken before the cook
 * starts and never reads the live network, and the result is published when the owning thread
 * calls processPendingCooks. The facade leaves that mode off and cooks on the calling thread,
 * so the geometry returned by cookDisplaySummary and cookDisplayBuffers is the geometry as of
 * the call. The returned buffers are copies that belong to the caller, and can be handed to
 * another thread (to be uploaded to the GPU, say) freely.
 *
 * @par Rendering
 * There is no Metal drawing here yet. The viewport renderer of helixGraphics will be driven
 * from this facade once the native application shows the viewport, and the buffers returned by
 * cookDisplayBuffers are what it will be fed.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace helix::core {

/** @name Results
 * @{
 */

/// @brief The outcome of a call that can fail.
struct Status
{
    /// @brief True when the call did what was asked.
    bool ok = true;
    /// @brief Why the call failed, empty when it did not.
    std::string error;
};

/// @brief A node id, as the engine hands it out. Zero is never a node.
using NodeId = std::uint64_t;

/** @} */

/** @name Starting up
 * @{
 */

/// @brief Where the engine finds what it loads at run time.
///
/// Every path may be left empty, which keeps the engine's own lookup (next to the executable
/// of an application bundle, or the build tree of a development run).
struct InitOptions
{
    /// @brief The folder holding one folder per node, each with a node.yaml.
    std::string nodesDirectory;
    /// @brief The folder holding the node library (libhelixOps.dylib).
    std::string librariesDirectory;
    /// @brief The folder that holds the daslang standard library, as `daslib`.
    std::string runtimeDirectory;
};

/// @brief Names where the engine loads from and loads every node type.
/// @note Call it once, before anything else. A second call does nothing and succeeds.
Status initialize(const InitOptions& options);

/// @brief Returns whether initialize succeeded.
bool isInitialized();

/// @brief Returns the version of the engine, such as "0.7.0".
std::string version();

/** @} */

/** @name Scene
 * @{
 */

/// @brief Replaces the scene with an empty one, and marks it unmodified.
Status newScene();

/// @brief Replaces the scene with the one stored in a .kut file.
/// @note A file that does not exist leaves the scene as it was. A file that exists and cannot
/// be read leaves an empty scene, since the engine clears the scene before it reads.
Status openScene(const std::string& path);

/// @brief Writes the scene to a .kut file, and marks it unmodified.
/// @note The write is atomic. The scene goes to a temporary file in the same folder, which
/// replaces the destination in one rename, so a failed write leaves the previous file whole.
/// @note The display node is not stored in the file, so it is not set again by openScene.
Status saveScene(const std::string& path);

/// @brief Returns whether the scene changed since it was created, opened or saved.
bool isSceneModified();

/** @} */

/** @name Graph
 * @{
 */

/// @brief One node of the graph.
struct NodeInfo
{
    NodeId id = 0;
    /// @brief The full type name, such as "helix::cube".
    std::string type;
    /// @brief The name of the node among its siblings, such as "cube1".
    std::string name;
    /// @brief The full path of the node, such as "/cube1".
    std::string path;
    /// @brief Where the node sits in the network editor.
    float x = 0.f;
    float y = 0.f;
};

/// @brief One link, from the output of a node into the input of another.
struct LinkInfo
{
    NodeId sourceNode = 0;
    std::uint32_t sourceOutput = 0;
    NodeId targetNode = 0;
    std::uint32_t targetInput = 0;
};

/// @brief The result of creating a node.
struct CreateNodeResult
{
    Status status;
    NodeId id = 0;
};

/// @brief Returns every node, in no particular order.
std::vector<NodeInfo> listNodes();

/// @brief Returns every link, in no particular order.
std::vector<LinkInfo> listLinks();

/// @brief Returns the full type name of every node that can be created.
std::vector<std::string> listNodeTypes();

/// @brief Creates a node at the root of the scene.
/// @param type The full type name, such as "helix::cube".
/// @param name The name to give it. Empty picks a free one such as "cube1". A name that is
/// taken, or that cannot name a node, is an error rather than being changed.
CreateNodeResult createNode(const std::string& type, const std::string& name, float x, float y);

/// @brief Deletes a node and the links it has.
Status deleteNode(NodeId node);

/// @brief Wires the output of one node into the input of another.
/// @note An input that was already wired is rewired.
Status connectNodes(
    NodeId sourceNode,
    std::uint32_t sourceOutput,
    NodeId targetNode,
    std::uint32_t targetInput
);

/// @brief Removes the link between two ports.
Status disconnectNodes(
    NodeId sourceNode,
    std::uint32_t sourceOutput,
    NodeId targetNode,
    std::uint32_t targetInput
);

/// @brief Moves a node in the network editor.
Status moveNode(NodeId node, float x, float y);

/// @brief Makes a node the one the viewport shows.
Status setDisplayNode(NodeId node);

/// @brief Returns the node the viewport shows, or 0 when there is none.
NodeId displayNode();

/** @} */

/** @name Parameters
 * @{
 */

/// @brief What a parameter holds.
enum class ParameterType : std::int32_t
{
    Float,
    Int,
    String,
    Toggle,
    Dropdown,
    Ramp,
    /// @brief Anything a panel does not edit as a value, such as a divider.
    Other
};

/// @brief One parameter of a node.
struct ParameterInfo
{
    /// @brief The name scripts and expressions use, such as "size".
    std::string name;
    /// @brief The name shown to a person, such as "Size".
    std::string label;
    ParameterType type = ParameterType::Other;
    /// @brief How many values the parameter holds, three for a vector.
    std::uint32_t componentCount = 1;
};

struct FloatValue
{
    Status status;
    float value = 0.f;
};

struct IntValue
{
    Status status;
    std::int64_t value = 0;
};

struct StringValue
{
    Status status;
    std::string value;
};

struct Vector3Value
{
    Status status;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/// @brief Returns the parameters of a node, in the order the node declares them. Empty for a
/// node that does not exist.
std::vector<ParameterInfo> listParameters(NodeId node);

/// @brief Reads one component of a float parameter.
FloatValue getFloat(NodeId node, const std::string& parameter, std::uint32_t component);
/// @brief Reads one component of an integer or toggle parameter.
IntValue getInt(NodeId node, const std::string& parameter, std::uint32_t component);
/// @brief Reads one component of a string or dropdown parameter.
StringValue getString(NodeId node, const std::string& parameter, std::uint32_t component);
/// @brief Reads a float parameter of three components.
Vector3Value getVector3(NodeId node, const std::string& parameter);

/// @brief Writes one component of a float parameter. The engine clamps it to the range the
/// parameter declares.
Status setFloat(NodeId node, const std::string& parameter, std::uint32_t component, float value);
/// @brief Writes one component of an integer or toggle parameter.
Status setInt(NodeId node, const std::string& parameter, std::uint32_t component, std::int64_t value);
/// @brief Writes one component of a string or dropdown parameter.
Status
setString(NodeId node, const std::string& parameter, std::uint32_t component, const std::string& value);
/// @brief Writes a float parameter of three components.
Status setVector3(NodeId node, const std::string& parameter, float x, float y, float z);

/** @} */

/** @name Time
 * @{
 */

/// @brief The frame the scene sits on and the range playback runs over.
struct TimeInfo
{
    float frame = 1.f;
    std::int32_t startFrame = 1;
    std::int32_t endFrame = 240;
    float fps = 24.f;
};

/// @brief Returns the current frame, the playback range and the frame rate.
TimeInfo timeInfo();

/// @brief Moves the scene to a frame, clamped to the playback range.
/// @note Moving through time does not mark the scene modified.
void setFrame(float frame);

/** @} */

/** @name Display geometry
 * @{
 */

/// @brief What the display node produced, in numbers.
struct GeometrySummary
{
    Status status;
    /// @brief The points of every mesh.
    std::uint64_t pointCount = 0;
    /// @brief The primitives (meshes, cameras) in the geometry.
    std::uint64_t primitiveCount = 0;
    /// @brief The faces of every mesh.
    std::uint64_t faceCount = 0;
    /// @brief The triangles the closed faces split into.
    std::uint64_t triangleCount = 0;
};

/// @brief The geometry of the display node, flattened for the GPU.
///
/// Vertex n is the n-th mesh vertex, counting on from the vertices of the meshes before it.
/// The index lists refer to the vertices.
struct DisplayBuffers
{
    Status status;
    /// @brief Three floats (x, y, z) per vertex.
    std::vector<float> positions;
    /// @brief Three floats (x, y, z) per vertex, parallel to positions.
    std::vector<float> normals;
    /// @brief Three vertex indices per triangle of the closed faces.
    std::vector<std::uint32_t> triangleIndices;
    /// @brief Two vertex indices per edge around the closed faces.
    std::vector<std::uint32_t> edgeIndices;
    /// @brief Two vertex indices per segment of the open faces.
    std::vector<std::uint32_t> lineIndices;
    /// @brief Three floats (x, y, z) per mesh point, for drawing the points.
    std::vector<float> pointPositions;
};

/// @brief Cooks the display node and counts what it produced.
/// @note Fails when no node is displayed or the node cannot be cooked.
GeometrySummary cookDisplaySummary();

/// @brief Cooks the display node and returns its geometry as flat buffers.
/// @note Fails when no node is displayed or the node cannot be cooked.
DisplayBuffers cookDisplayBuffers();

/** @} */

} // namespace helix::core
