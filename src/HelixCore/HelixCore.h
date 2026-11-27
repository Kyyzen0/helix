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
 * The viewport renderer of helixGraphics is driven from the Viewport section below. Metal
 * objects cross this boundary as opaque `void*` pointers, so that no Metal, metal-cpp, glm or
 * Eigen header is included here, and the section says for each pointer who retains it and for
 * how long.
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
/// @note The display node is stored in the file, and shown again by openScene. A file from
/// before the display node was stored shows the last top level node whose output nothing reads.
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

/// @brief Makes the viewport show no node. Succeeds when none was shown.
Status clearDisplayNode();

/// @brief Returns the node the viewport shows, or 0 when there is none.
NodeId displayNode();

/** @} */

/** @name Import
 * @{
 *
 * Helix reads other tools' files through one node, `helix::geometryImport`, which picks its
 * reader by extension: Assimp for glTF, GLB (Draco compression included), FBX, STL, PLY,
 * Collada, 3DS, 3MF, DXF, X3D and X, the engine's own reader for OBJ, Model I/O for USD, USDA,
 * USDC and USDZ, and Blender running in the background for BLEND. Only the geometry is imported:
 * points, faces, normals, UVs (`uv`), vertex colours (`Cd`) and a `material_index` per face.
 * Skeletons, animations, textures and materials are left out.
 */

/// @brief The extensions the import reads, lower case and with the dot (".glb", ".usdz").
std::vector<std::string> supportedImportExtensions();

/// @brief Names the folder where the conversions of .blend files are kept between runs, which is
/// created when it does not exist. Without one they go to a folder in the temporary directory.
Status setCacheDirectory(const std::string& directory);

/// @brief Reads a file to check it, then creates an import node that points at it.
///
/// The node sits at the root of the scene with the default settings, and is not made the display
/// node. The file is read once here and kept in memory, so the node's first cook does not read it
/// again. A file that cannot be imported creates no node: the status says why, in words for a
/// person, such as the list of formats for an extension Helix does not read, or that Blender is
/// needed for a .blend file.
/// @note It never writes to the file.
CreateNodeResult importFile(const std::string& path);

/// @brief A sentence on what the last successful importFile found: the counts, what the file
/// carries (normals, UVs, colours), the first base colour, and what was left out (animations,
/// skins). Empty before any import.
std::string lastImportSummary();

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

/// @brief The box around some geometry, in world units.
struct Bounds
{
    /// @brief False when there is no geometry, and the corners mean nothing.
    bool valid = false;
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;
};

/// @brief Which elements of the geometry own an attribute.
enum class AttributeOwner : std::int32_t
{
    Point,
    Vertex,
    Face,
    Primitive
};

/// @brief What an attribute stores per element.
enum class AttributeValueType : std::int32_t
{
    Int,
    Float,
    Vector3,
    Bool,
    Matrix4
};

/// @brief One column of the geometry a node produced.
struct AttributeInfo
{
    AttributeOwner owner = AttributeOwner::Point;
    /// @brief The name, such as "Position" (the point position every mesh has).
    std::string name;
    AttributeValueType type = AttributeValueType::Float;
    /// @brief How many numbers one element holds: 1, 3 for a vector, 16 for a matrix.
    std::uint32_t componentCount = 1;
    /// @brief How many elements the attribute has, over every primitive that carries it.
    std::uint64_t elementCount = 0;
};

/// @brief What any node produced, in numbers: the counts, the bounds and the attribute list.
struct GeometryReport
{
    Status status;
    std::uint64_t pointCount = 0;
    std::uint64_t primitiveCount = 0;
    std::uint64_t vertexCount = 0;
    std::uint64_t faceCount = 0;
    std::uint64_t triangleCount = 0;
    /// @brief The box around the points, not valid when there are none.
    Bounds bounds;
    /// @brief Every attribute (the position included), once per owner and name.
    std::vector<AttributeInfo> attributes;
};

/// @brief The values of a run of elements of one attribute.
struct AttributeValues
{
    Status status;
    AttributeValueType type = AttributeValueType::Float;
    std::uint32_t componentCount = 1;
    /// @brief How many elements the attribute has in all, so a caller can page.
    std::uint64_t totalCount = 0;
    /// @brief The index of the first element returned.
    std::uint64_t start = 0;
    /// @brief The values of a Float, Vector3 or Matrix4 attribute, `componentCount` per element
    /// (a matrix row by row). Empty for the other types.
    std::vector<float> floats;
    /// @brief The values of an Int or Bool attribute (a bool as 0 or 1). Empty for the others.
    std::vector<std::int64_t> ints;
};

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

/// @brief Cooks any node and reports what it produced: the counts, the bounds and the
/// attributes.
/// @note Fails when the node does not exist or cannot be cooked. The cook runs on the calling
/// thread, at the current frame, and does not change the display node. A node that produced
/// more than one primitive (an import that does not merge its meshes) reports their sum.
GeometryReport cookNodeReport(NodeId node);

/// @brief Cooks any node and reads a run of values of one attribute.
/// @param owner which elements own the attribute.
/// @param name the attribute, such as "Position".
/// @param start the index of the first element.
/// @param count how many elements at most. Fewer come back at the end of the attribute, and none
/// when `start` is past it.
/// @note When the node produced several primitives, their elements follow one another in the
/// order of the primitives, and a primitive that lacks the attribute adds none. Fails when the
/// node has no attribute of that name on that owner.
AttributeValues readAttribute(
    NodeId node,
    AttributeOwner owner,
    const std::string& name,
    std::uint64_t start,
    std::uint64_t count
);

/// @brief Cooks the display node and counts what it produced.
/// @note Fails when no node is displayed or the node cannot be cooked.
GeometrySummary cookDisplaySummary();

/// @brief Cooks the display node and returns its geometry as flat buffers.
/// @note Fails when no node is displayed or the node cannot be cooked.
DisplayBuffers cookDisplayBuffers();

/** @} */

/** @name Viewport
 * @{
 *
 * @par Metal objects across the boundary
 * - The `MTLDevice` given to startViewport is borrowed for the call. The facade retains its own
 *   reference and keeps it until stopViewport.
 * - The `MTLCommandBuffer` given to encodeViewport is borrowed for the call. The facade neither
 *   commits it nor waits for it. It attaches a completion handler, which the command buffer
 *   itself retains until the GPU has finished, so the buffer is free to be committed and
 *   released as usual.
 * - The `MTLTexture` returned in ViewportFrame is owned by the facade, which does not retain it
 *   for the caller. It is valid until the next encodeViewport that changes the frame size, until
 *   stopViewport, or until the engine shuts down, whichever comes first. A caller that encodes
 *   its own pass into the same command buffer (to copy or sample the texture) takes its own
 *   reference with `takeUnretainedValue` on the Swift side, and the command buffer then holds
 *   it until the pass has run. A texture replaced by a resize stays alive until the frames
 *   that read it have finished.
 *
 * @par The camera
 * The viewport has one orbit camera, kept by the facade. It looks at a centre point from a
 * distance. The functions below move it in the units a pointer or a trackpad gives, which are
 * pixels of the view, and none of them redraws: the caller asks for a new frame.
 */

/// @brief What the viewport holds after the display node was cooked.
struct ViewportGeometryInfo
{
    /// @brief Fails when no node is displayed or the node cannot be cooked. The viewport then
    /// holds no geometry and draws the grid alone.
    Status status;
    std::uint64_t pointCount = 0;
    std::uint64_t triangleCount = 0;
    Bounds bounds;
};

/// @brief Whether the viewport draws the edges of the mesh over its shading.
enum class WireframeMode
{
    /// @brief The wireframe is drawn while the mesh is light enough to read through it: it is
    /// left out above 50 000 triangles and fades out as the edges come close on screen.
    Auto,
    /// @brief Every edge is drawn, whatever the density of the mesh.
    On,
    /// @brief No edge is drawn.
    Off,
};

/// @brief What one viewport frame is drawn with.
struct ViewportFrameOptions
{
    /// @brief The size of the frame in device pixels.
    std::uint32_t pixelWidth = 0;
    std::uint32_t pixelHeight = 0;
    /// @brief The number of device pixels per point of the interface.
    float pixelRatio = 1.f;
    /// @brief The colour the frame is cleared to, each channel from 0 to 1.
    float backgroundRed = 0.13f;
    float backgroundGreen = 0.14f;
    float backgroundBlue = 0.16f;
    float backgroundAlpha = 1.f;
    /// @brief The colour the geometry is shaded in, each channel from 0 to 1.
    float geometryRed = 0.62f;
    float geometryGreen = 0.64f;
    float geometryBlue = 0.68f;
    WireframeMode wireframe = WireframeMode::Auto;
    bool pointsVisible = false;
};

/// @brief The texture one viewport frame was recorded into.
struct ViewportFrame
{
    Status status;
    /// @brief An `id<MTLTexture>` in RGBA8 (unorm), or null when nothing was drawn. See the
    /// notes above for how long it is valid.
    void* colorTexture = nullptr;
    std::uint32_t pixelWidth = 0;
    std::uint32_t pixelHeight = 0;
};

/// @brief Starts the viewport renderer on a Metal device, with the shaders in
/// `Contents/Resources/helix.metallib` of the application bundle.
/// @param metalDevice an `id<MTLDevice>`, borrowed for the call, or null for the default device
/// of the machine.
/// @note The device must be the one the caller's own Metal objects (its command queue and its
/// drawable textures) come from, since the texture is sampled by the caller's command buffer.
/// Starting twice stops the first renderer.
Status startViewport(void* metalDevice);

/// @brief Releases the renderer and the device. The camera and the geometry are kept.
void stopViewport();

/// @brief Returns whether startViewport succeeded and stopViewport has not been called since.
bool isViewportRunning();

/// @brief Cooks the display node and gives its geometry to the viewport.
/// @note Call it when the scene or the display node changed. A change of the camera needs no
/// cook. The bounds returned are what frameViewport frames.
ViewportGeometryInfo updateViewportGeometry();

/// @brief Turns the camera around its centre by a drag of that many pixels, with the pointer
/// convention of a window: x grows to the right and y grows downward.
void orbitViewport(float deltaX, float deltaY);

/// @brief Slides the camera and its centre across the view by a drag of that many pixels, so
/// the scene follows the pointer.
void panViewport(float deltaX, float deltaY);

/// @brief Moves the camera toward its centre for a negative amount and away for a positive
/// one. The amount is in the same unit as a step of a pinch or a wheel, a tenth of the distance
/// per unit.
void dollyViewport(float amount);

/// @brief Turns the camera to an absolute direction, keeping its centre and distance.
/// @param azimuthDegrees the angle around the vertical axis. The camera of a new viewport sits at
/// -135, and 0 looks down the negative z axis.
/// @param elevationDegrees the angle above the horizontal plane, held between -89 and 89 so the
/// view does not flip. The camera of a new viewport sits at 20.
/// @note Call it before frameViewport, which frames the geometry at the angles of the moment:
/// framing first and orbiting after can turn a corner of the box out of the view.
void orbitViewportTo(float azimuthDegrees, float elevationDegrees);

/// @brief Puts the camera back where a new viewport starts.
void resetViewport();

/// @brief Moves the camera so the geometry cached by the last updateViewportGeometry fills the
/// view, keeping the angles. Does nothing when the viewport holds no geometry.
/// @param aspect the width of the view divided by its height.
/// @return Whether there was geometry to frame.
bool frameViewport(float aspect);

/// @brief Records one frame into a command buffer of the caller.
///
/// The frame is drawn by the GPU when the caller commits the command buffer. Nothing is
/// committed and nothing is waited for here, so it can run on the interactive path.
/// @param commandBuffer an `id<MTLCommandBuffer>` with no encoder open, borrowed for the call.
/// @return The texture the frame is drawn into. The caller copies or samples it in a later pass
/// of the same command buffer.
ViewportFrame encodeViewport(void* commandBuffer, const ViewportFrameOptions& options);

/// @brief Cooks the display node, draws it off screen and writes a PNG.
///
/// This waits for the GPU, and is meant for a capture, a test or an AI checking its work, not
/// for the interactive path. It uses a renderer of its own on the default device, so it works
/// with no window and does not disturb a running viewport.
/// @param path where the PNG goes, replaced when it exists.
/// @param options the size and the colours. A size of zero is an error.
/// @param useViewportCamera whether to look through the camera of the viewport. When false,
/// the camera is a new one that frames the geometry from the default angles.
Status
renderToImage(const std::string& path, const ViewportFrameOptions& options, bool useViewportCamera);

/** @} */

} // namespace helix::core
