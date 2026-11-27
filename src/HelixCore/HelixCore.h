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

/// @brief The result of capturing the scene.
struct SceneSnapshot
{
    Status status;
    /// @brief The scene as the text of a .kut file.
    std::string data;
};

/// @brief Captures the scene in memory, as the text saveScene would write.
///
/// The text holds what a saved file holds: the nodes with their names, positions and parameters,
/// the links, the display node, the keyframes and the time. The modified state is left alone.
SceneSnapshot captureScene();

/// @brief Replaces the scene with one captured by captureScene.
/// @note The node ids are handed out again, in an order that is not the order they had, so
/// a caller that holds ids matches the nodes by path across a restore. Names, paths and
/// positions come back as they were. The modified state is left as it was before the call,
/// since going back to a capture says nothing about whether the file on disk matches it. Text
/// that cannot be read leaves an empty scene, like openScene.
Status restoreScene(const std::string& data);

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
    /// @brief Why the last cook of the node failed, in a sentence for a person. Empty when it
    /// cooked cleanly, and also when it has not cooked since it last changed (a node nothing
    /// displayed or asked for is not cooked, so it has no verdict yet).
    std::string error;
    /// @brief What the last cook of the node warned about. The cook went on in spite of it.
    std::vector<std::string> warnings;
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

/// @brief One input port of a node type.
struct PortInfo
{
    /// @brief What the port is for, such as "Geometry" or "Mask".
    std::string label;
    /// @brief Whether the port takes any number of inputs. Only the last port can.
    bool multiInput = false;
    /// @brief Whether the node cooks with nothing wired into the port.
    bool optional = false;
};

/// @brief What a person or an agent needs to know about a node type to create it and wire it.
///
/// Inputs are numbered from zero in the order listed, and a multi input port takes every input
/// from its own position onward. Outputs have no names: they are numbered from zero.
struct NodeTypeInfo
{
    /// @brief The full type name createNode takes, such as "helix::cube".
    std::string name;
    /// @brief The name shown to a person, such as "Cube".
    std::string label;
    /// @brief The words a search matches, such as "primitive".
    std::vector<std::string> tags;
    /// @brief The input ports, in the order their inputs run.
    std::vector<PortInfo> inputs;
    /// @brief The number of outputs the node has.
    std::uint32_t outputCount = 1;
};

/// @brief The result of looking up a node type.
struct NodeTypeInfoResult
{
    Status status;
    NodeTypeInfo info;
};

/// @brief Returns what is known about one node type, or a failed status when there is none.
/// @note A tab menu alias, such as "helix::mountain", answers with its own label and tags and
/// the ports of the type it creates.
NodeTypeInfoResult nodeTypeInfo(const std::string& type);

/// @brief Returns nodeTypeInfo for every type listNodeTypes lists, in the same order.
std::vector<NodeTypeInfo> listNodeTypeInfos();

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
 * points, faces, normals, UVs (`uv`), vertex colours (`Cd`), the materials (base colour factor and
 * base colour texture, decoded to pixels) and a `material_index` per face that points into the
 * mesh's table of materials (see MaterialInfo). Skeletons, animations and the other texture maps
 * (normal, roughness, metallic, emissive, occlusion) are left out.
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

/// @brief One entry of a dropdown: the value the node stores, and the text a person reads.
struct ParameterOption
{
    std::string value;
    std::string label;
};

/// @brief One option of a parameter style, such as the icon of a boolIcon or the owners of an
/// attribute.
struct ParameterStyleOption
{
    std::string name;
    std::string value;
};

/// @brief One parameter of a node, with everything a panel needs to edit it.
struct ParameterInfo
{
    /// @brief The name scripts and expressions use, such as "size".
    std::string name;
    /// @brief The name shown to a person, such as "Size".
    std::string label;
    ParameterType type = ParameterType::Other;
    /// @brief How many values the parameter holds, three for a vector.
    std::uint32_t componentCount = 1;

    /// @brief A sentence on what the parameter does. Empty when the node says nothing.
    std::string tooltip;
    /// @brief Longer documentation, empty when there is none.
    std::string documentation;
    /// @brief How the node asks for the parameter to be drawn: "" (the plain widget), "xyz",
    /// "rangeCircle", "boolIcon", "boolSwitch", "attribute" or "code".
    std::string style;
    /// @brief The options the style carries, such as `icon` for boolIcon, `caption` and
    /// `ordered` for rangeCircle, `owners` and `attributeTypes` for attribute.
    std::vector<ParameterStyleOption> styleOptions;

    /// @brief Whether a numeric parameter has a range, which every float and integer does (a
    /// node that says nothing gets 0 to 10, with neither end locked).
    bool hasRange = false;
    /// @brief The range every component shares (a node.yaml gives one range to a parameter).
    /// An unlocked end is where a slider stops, and a person may type past it. A locked end is a
    /// limit: the engine pulls a value back to it.
    float rangeMin = 0.f;
    float rangeMax = 10.f;
    bool minLocked = false;
    bool maxLocked = false;

    /// @brief What a dropdown offers, in order. Empty for any other parameter.
    std::vector<ParameterOption> options;

    /// @brief The defaults, one per component, in the list the type reads: floats for a float,
    /// integers for an integer or a toggle, strings for a string or a dropdown (the stored value
    /// of the option). The others are empty.
    std::vector<float> defaultFloats;
    std::vector<std::int64_t> defaultInts;
    std::vector<std::string> defaultStrings;

    /// @brief Whether the parameter, or a group around it, is hidden by its hide condition now.
    bool hidden = false;
    /// @brief Whether the parameter, and every group around it, is enabled by its disable
    /// condition now. A disabled parameter is shown and cannot be edited.
    bool enabled = true;
    /// @brief Whether the label is left out because the widget carries its own.
    bool labelHidden = false;
    /// @brief The labels of the groups around the parameter, outermost first, joined with " / ".
    /// Empty at the top level.
    std::string group;

    /// @brief The expression driving each component, one entry per component and an empty
    /// string where there is none.
    std::vector<std::string> expressions;
    /// @brief What is wrong with each component's expression, an empty string where it is fine
    /// or there is none. A broken expression reads as zero.
    std::vector<std::string> expressionErrors;
    /// @brief The components that hold keyframes, by index. Their value follows the timeline.
    /// Writing one sets or replaces the key at the current frame (see setFloat).
    std::vector<std::uint32_t> animatedComponents;
    /// @brief The animated components that have a key on the current frame exactly, which a
    /// panel draws as a filled diamond. A subset of `animatedComponents`.
    std::vector<std::uint32_t> keyedComponents;
};

/// @brief What an entry of the layout of a node's parameters is.
enum class ParameterLayoutKind : std::int32_t
{
    /// @brief A parameter, named by `name`.
    Parameter,
    /// @brief The start of a group. Every entry up to its GroupEnd belongs to it.
    GroupBegin,
    GroupEnd,
    /// @brief A heading between parameters.
    Divider
};

/// @brief One entry of the order a node's parameters are shown in.
struct ParameterLayoutItem
{
    ParameterLayoutKind kind = ParameterLayoutKind::Parameter;
    /// @brief The parameter name, or the name of the group or divider.
    std::string name;
    /// @brief The text of a group or a divider, and the label of a parameter.
    std::string label;
    /// @brief The icon of a divider (a Lucide name such as "tag"), empty when there is none.
    std::string icon;
    /// @brief Whether a group lays its members out side by side (true) or one under the other.
    bool horizontal = false;
    /// @brief Whether a group shows its label beside its row instead of above it.
    bool labelInline = false;
    /// @brief Whether the entry, or a group around it, is hidden now.
    bool hidden = false;
    /// @brief Whether the entry, and every group around it, is enabled now.
    bool enabled = true;
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

/// @brief Returns the parameters of a node, in the order the node declares them, with what a
/// panel needs to edit each (range, options, defaults, expressions, whether it is hidden or
/// enabled now). Empty for a node that does not exist.
/// @note Groups are not parameters and are left out, a divider is listed as ParameterType::Other.
/// The groups and the dividers are in listParameterLayout.
std::vector<ParameterInfo> listParameters(NodeId node);

/// @brief Returns the order a node's parameters are shown in: groups opened and closed, dividers
/// and parameters, as the node declares them, with the hide and disable conditions evaluated now.
/// @note Empty for a node that does not exist. The parameters are the names listParameters gives.
std::vector<ParameterLayoutItem> listParameterLayout(NodeId node);

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
/// @note A component that holds keyframes follows the timeline, so the write would be undone at
/// the next frame. Instead it sets the key on the current frame, or replaces the one that is
/// there, with the value (the "auto key" of other tools). A new key takes the interpolation of the
/// key before it, or smooth when it is the first. The keys on other frames are left alone, and the
/// parameter keeps showing the value of the track at the current frame.
Status setFloat(NodeId node, const std::string& parameter, std::uint32_t component, float value);
/// @brief Writes one component of an integer or toggle parameter.
Status setInt(NodeId node, const std::string& parameter, std::uint32_t component, std::int64_t value);
/// @brief Writes one component of a string or dropdown parameter.
Status
setString(NodeId node, const std::string& parameter, std::uint32_t component, const std::string& value);
/// @brief Writes a float parameter of three components. A component that holds keyframes is keyed
/// at the current frame, as setFloat does.
Status setVector3(NodeId node, const std::string& parameter, float x, float y, float z);

/// @brief Makes a component read from a daslang expression, such as "sin($F * 0.1)".
///
/// The text is kept even when it does not compile, the way the Qt editor did: the person is
/// typing, and what they typed is not thrown away. The status is therefore ok whenever the
/// expression was stored, and what is wrong with it is read back from
/// ParameterInfo::expressionErrors (a broken expression reads as zero, so the node is not left
/// with an unreadable value). It fails for a parameter that takes no expression (a ramp, a
/// divider), a component out of range, a component that holds keyframes (the timeline would
/// overwrite the expression), an empty text (use clearExpression) and a text over 4096
/// characters.
Status setExpression(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    const std::string& expression
);

/// @brief Takes the expression off a component, which reads the literal it holds again.
/// Succeeds when there was none.
Status clearExpression(NodeId node, const std::string& parameter, std::uint32_t component);

/// @brief Puts every component of a parameter back to the default the node declares, and takes
/// its expressions off. A ramp goes back to its default points.
/// @note Fails for a parameter that holds keyframes, which would only be overwritten again: clear
/// its keyframes first.
Status resetParameter(NodeId node, const std::string& parameter);

/// @brief One control point of a ramp.
struct RampPoint
{
    /// @brief Where the point sits along the ramp, usually from 0 to 1.
    float position = 0.f;
    float value = 0.f;
    /// @brief How the curve leaves the point: "constant", "linear" or "bspline".
    std::string interpolation = "linear";
};

/// @brief A list of ramp points, which gives Swift a name for the vector it hands to setRamp.
using RampPointList = std::vector<RampPoint>;

/// @brief The points of a ramp parameter, in the order the node stores them.
struct RampInfo
{
    Status status;
    std::vector<RampPoint> points;
    /// @brief The interpolations a point can take, in order.
    std::vector<ParameterOption> interpolations;
};

/// @brief Reads a ramp parameter.
RampInfo getRamp(NodeId node, const std::string& parameter);

/// @brief Replaces the points of a ramp parameter.
/// @note Needs between 2 and 64 points, finite positions and values, and interpolations getRamp
/// lists: anything else is refused and the ramp is left as it was, since a ramp the sampler
/// cannot read has to be an error here, not a changed point there.
Status setRamp(NodeId node, const std::string& parameter, const std::vector<RampPoint>& points);

/// @brief The names of the attributes a parameter of the `attribute` style can offer.
struct AttributeNames
{
    Status status;
    std::vector<std::string> names;
};

/// @brief Lists the attributes on the geometry wired into the node's first input that match the
/// owners and the types the style of a parameter asks for, once each and sorted.
/// @note Cooks the input if it is not cooked yet. Fails for a node that has no input wired or a
/// parameter that is not of the attribute style.
AttributeNames listAttributeSuggestions(NodeId node, const std::string& parameter);

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
/// @note Moving through time does not mark the scene modified. Every keyframed parameter takes
/// its value at the frame, and only the nodes that read the time cook again: what a node produces
/// is a pure function of the frame, so jumping back gives what playing forward gave.
void setFrame(float frame);

/// @brief Sets the first and the last frame of the playback range. The frame is held inside it.
/// @note Fails when `start` is not below `end`, or a frame is further than a million from zero.
/// Marks the scene modified, since the range is saved in the file.
Status setFrameRange(std::int32_t start, std::int32_t end);

/// @brief Sets how many frames make up a second, from above 0 to 1000 at most. The nodes that
/// read the time cook again, since a simulation steps by the length of a frame.
/// @note Marks the scene modified, since the rate is saved in the file.
Status setFps(float fps);

/** @} */

/** @name Cook cache
 * @{
 *
 * A node that moves with the scene time (it reads the frame, a keyframe or an expression drives
 * one of its parameters, or something wired into it does) keeps what it produced at every frame it
 * has cooked, so going back to a frame costs a lookup instead of a cook. The solvers of the
 * simulation node also save their state every few frames, so a frame not visited yet is reached
 * from the nearest saved state rather than from the first frame. What is kept is the same, bit for
 * bit, as what a cook makes: any change to a parameter, a link or a keyframe of a node (or of
 * anything upstream of it) drops what it held. One budget bounds all of it, and the entry used
 * longest ago goes first.
 */

/// @brief What the cook cache holds and how it has been used.
struct CookCacheInfo
{
    /// @brief The estimated size of everything the cache holds, in bytes.
    std::uint64_t bytesUsed = 0;
    /// @brief The most the cache may hold, in bytes. Zero means the cache is off.
    std::uint64_t budget = 0;
    /// @brief How many things the cache holds, cooks and solver states together.
    std::uint64_t entries = 0;
    /// @brief How many of them are what a node produced at one frame.
    std::uint64_t frameEntries = 0;
    /// @brief How many of them are the saved state of a solver.
    std::uint64_t stateEntries = 0;
    /// @brief How many cooks were answered from the cache since the process started.
    std::uint64_t hits = 0;
    /// @brief How many times the cache was asked and held nothing.
    std::uint64_t misses = 0;
    /// @brief How many times a simulation resumed from a saved state instead of its first frame.
    std::uint64_t stateHits = 0;
};

/// @brief Returns what the cook cache holds and how it has been used.
CookCacheInfo cookCacheInfo();

/// @brief Sets the most the cook cache may hold, in bytes (1 GiB at the start). What no longer
/// fits is dropped, the entry used longest ago first. Zero turns the cache off and empties it.
/// @note The size is an estimate from the size of the attributes, which errs on the high side:
/// two frames that share an attribute are counted as if they held it twice.
void setCookCacheBudget(std::uint64_t bytes);

/// @brief Drops everything the cook cache holds. The next visit to a frame cooks it again.
void clearCookCache();

/// @brief The whole frames a node has a cached cook for, in increasing order. Empty for a node
/// that does not exist, one that does not move with the time (nothing is kept for it), and one
/// that has not been cooked at any frame yet.
/// @note Reading it never cooks. The timeline shows it for the display node.
std::vector<std::int32_t> cachedFrames(NodeId node);

/** @} */

/** @name Keyframes
 * @{
 *
 * A float parameter component can hold keyframes: values pinned to frames, with a way to move from
 * one to the next. The value of the component is then a pure function of the frame. Every function
 * here works on one component of a float parameter, and fails with a sentence for any other
 * parameter, a component out of range, a node that is not there or a value that is not a number.
 * After any change the parameter reads the value its track holds at the current frame, and the node
 * and what is wired after it cook again. Frames are floats, so a key can sit between two frames.
 */

/// @brief How a value moves from one key to the next one. It belongs to the key it starts from.
enum class Interpolation : std::int32_t
{
    /// @brief The value holds until the next key, then jumps.
    Step,
    /// @brief The value moves at a constant rate.
    Linear,
    /// @brief The value eases in and out of the segment.
    Smooth
};

/// @brief One key of a track.
struct KeyframeInfo
{
    float frame = 0.f;
    float value = 0.f;
    Interpolation interpolation = Interpolation::Smooth;
};

/// @brief The keys of one component of one parameter.
struct KeyframeTrackInfo
{
    std::string parameter;
    std::uint32_t component = 0;
    /// @brief In order of frame.
    std::vector<KeyframeInfo> keys;
    /// @brief The value the component held before it was keyed, which clearKeyframes puts back.
    float baseValue = 0.f;
};

/// @brief Sets a key, or replaces the one on that frame.
/// @note The value is held to the locked ends of the range of the parameter, like setFloat. The
/// first key of a component remembers the value (and the expression) it held, for clearKeyframes.
Status setKeyframe(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame,
    float value,
    Interpolation interpolation
);

/// @brief Sets a key on a frame with the value the component has now, which is what "Set
/// Keyframe" of a panel does. A key already on the frame keeps its interpolation, a new one takes
/// the one of the key before it, or smooth for the first.
Status setKeyframeAtCurrentValue(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame
);

/// @brief Removes the key on a frame. Fails when there is none. Removing the last key of a
/// component puts back the value and the expression it held before it was keyed.
Status removeKeyframe(NodeId node, const std::string& parameter, std::uint32_t component, float frame);

/// @brief Moves a key to another frame, keeping its value and interpolation. Fails when there is no
/// key on `fromFrame` or one on `toFrame` already, which is never replaced.
Status moveKeyframe(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float fromFrame,
    float toFrame
);

/// @brief Changes how the value leaves the key on a frame. Fails when there is none.
Status setKeyframeInterpolation(
    NodeId node,
    const std::string& parameter,
    std::uint32_t component,
    float frame,
    Interpolation interpolation
);

/// @brief Removes every key of a component, and puts back the value and the expression it held
/// before it was keyed. Succeeds when there were none.
Status clearKeyframes(NodeId node, const std::string& parameter, std::uint32_t component);

/// @brief The tracks of a node, by parameter then component. Empty for a node that has none.
std::vector<KeyframeTrackInfo> listKeyframes(NodeId node);

/// @brief The nodes that hold at least one key, in no particular order.
std::vector<NodeId> animatedNodes();

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
    /// @brief True when primitives carry this name with different types or component counts.
    /// readAttribute refuses such an attribute, so the type and the component count describe
    /// only the first primitive that has it.
    bool mixedTypes = false;
};

/// @brief One material of the geometry a node produced, as the import read it.
struct MaterialInfo
{
    std::string name;
    /// @brief The base colour factor, linear, red green blue alpha from 0 to 1.
    float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
    /// @brief True when the material has a decoded base colour texture.
    bool hasTexture = false;
    /// @brief The size of the texture in pixels, 0 when there is none.
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    /// @brief Where the texture came from: a file path, or "<file>#embedded:N" for a picture kept
    /// inside the model file. Empty when there is no texture.
    std::string textureSource;
    /// @brief Empty, or what went wrong with a texture the file referred to and the import could
    /// not load (the material keeps its base colour factor).
    std::string missingTexture;
};

/// @brief One clip of a skeleton: an animation sampled as a function of time.
struct ClipInfo
{
    std::string name;
    /// @brief The length of the clip in seconds.
    float duration = 0.f;
    /// @brief How many tracks (translation, rotation or scale of a joint) hold keys.
    std::uint64_t channelCount = 0;
};

/// @brief What a skeleton (a mesh whose points are the joints) carries, when a primitive of the
/// geometry is one.
struct SkeletonInfo
{
    /// @brief True when a primitive of the geometry is a skeleton.
    bool present = false;
    std::uint64_t jointCount = 0;
    /// @brief The joints that have no parent.
    std::uint64_t rootCount = 0;
    std::vector<ClipInfo> clips;
};

/// @brief Where one primitive's elements sit among the elements of the whole node output, which
/// are numbered one primitive after the other, as readAttribute reads them.
struct PrimitiveRange
{
    std::uint64_t pointStart = 0;
    std::uint64_t pointCount = 0;
    std::uint64_t vertexStart = 0;
    std::uint64_t vertexCount = 0;
    std::uint64_t faceStart = 0;
    std::uint64_t faceCount = 0;
};

/// @brief What any node produced, in numbers: the counts, the bounds and the attribute list.
struct GeometryReport
{
    Status status;
    /// @brief The mark of the cook this report describes, as nodeCookGeneration gives it.
    std::uint64_t generation = 0;
    /// @brief One entry per primitive, in the order readAttribute walks them, so a table can say
    /// which primitive a row belongs to.
    std::vector<PrimitiveRange> primitives;
    std::uint64_t pointCount = 0;
    std::uint64_t primitiveCount = 0;
    std::uint64_t vertexCount = 0;
    std::uint64_t faceCount = 0;
    std::uint64_t triangleCount = 0;
    /// @brief The box around the points, not valid when there are none.
    Bounds bounds;
    /// @brief Every attribute (the position included), once per owner and name.
    std::vector<AttributeInfo> attributes;
    /// @brief The materials of the meshes, each once even when several meshes share it, in the
    /// order they are first met. A face's `material_index` points into the table of its own mesh,
    /// not into this list.
    std::vector<MaterialInfo> materials;
    /// @brief The first skeleton among the primitives, when there is one.
    SkeletonInfo skeleton;
};

/// @brief The values of a run of elements of one attribute.
struct AttributeValues
{
    Status status;
    /// @brief The mark of the cook these values come from, as nodeCookGeneration gives it. When it
    /// is not the one a cache of earlier pages holds, they are out of date.
    std::uint64_t generation = 0;
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

/// @brief Returns a number that changes every time the node cooks and is never shared by two cooks
/// of any node, or 0 for a node that has not cooked or does not exist.
/// @note A caller that keeps what readAttribute returned asks again whenever this is not the number
/// it saw. Reading it never cooks. cookNodeReport and readAttribute cook a node only when it is
/// dirty, so asking for a page of a node that is clean reads what is there and costs no cook.
std::uint64_t nodeCookGeneration(NodeId node);

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
    /// @brief The colour of the ground grid lines, each channel from 0 to 1.
    float gridRed = 0.227f;
    float gridGreen = 0.227f;
    float gridBlue = 0.275f;
    WireframeMode wireframe = WireframeMode::Auto;
    bool pointsVisible = false;
    /// @brief Whether the faces are drawn with the base colour and texture of their material.
    /// When false they take the geometry colour above.
    bool materialsVisible = true;
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
