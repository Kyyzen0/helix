#pragma once

/**
 * @file HelixCoreInternal.h
 * @brief What the files behind the facade share. Not part of the facade: a Swift application
 * never imports this header.
 */

#include "HelixCore/HelixCore.h"
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace helix {
class NodePacket;
namespace prm {
class NodeParameter;
}
} // namespace helix

namespace helix::core::detail {

inline Status success() { return Status{true, {}}; }

inline Status failure(std::string message) { return Status{false, std::move(message)}; }

/// @brief Runs an engine call, turning whatever it throws into a failed status.
inline Status guarded(const std::function<Status()>& action)
{
    try
    {
        return action();
    }
    catch (const std::exception& error)
    {
        return failure(error.what());
    }
    catch (...)
    {
        return failure("The engine failed for an unknown reason.");
    }
}

/// @brief Cooks a node on the calling thread and returns what its first output produced.
Status cookNodePacket(NodeId node, std::shared_ptr<const NodePacket>& packet);

/// @brief Cooks the display node on the calling thread and returns what it produced.
Status cookDisplayPacket(std::shared_ptr<const NodePacket>& packet);

/// @brief Finds a parameter of a node, or says why there is none (the engine is not started,
/// the node does not exist, the node has no such parameter).
Status findNodeParameter(NodeId node, const std::string& name, std::shared_ptr<prm::NodeParameter>& out);

/// @brief Fails when the engine is not started or the node does not exist.
Status requireNodeInScene(NodeId node);

/// @brief Notes that the scene has changed since it was saved.
void noteSceneModified();

/// @brief Empties the engine's own undo stack, which nothing reads (see HelixCore.cpp).
void dropEngineUndo();

/// @brief Waits for a background cook to finish and turns background cooking off, so the calling
/// thread can edit the network.
void ensureSynchronousCooking();

/// @brief Pulls a value back to the locked ends of the range a component declares, as a literal
/// written through the facade is.
float holdFloatToRange(prm::NodeParameter& parameter, std::uint32_t component, float value);

/// @brief Whether a component of a node's float parameter holds keyframes.
bool isKeyed(NodeId node, const std::string& parameter, std::uint32_t component);

/// @brief Writes a value into a component that holds keyframes, by setting the key on the current
/// frame (or replacing the one there). See setFloat in HelixCore.h.
Status keyAtCurrentFrame(NodeId node, const std::string& parameter, std::uint32_t component, float value);

} // namespace helix::core::detail
