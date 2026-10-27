#pragma once

#include "Engine/Core/Types.h"
#include "Engine/Parameter/Ramp.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace helix::async {

/**
 * @brief One parameter's value, as the cook will read it.
 *
 * The three lists hold the value in each of the forms a parameter can be read
 * in, so a cook reads exactly what it would read from the live parameter.
 */
struct CookParameterValue
{
    /// @brief Which kind of value the parameter holds.
    prm::ValueType valueType = prm::ValueType::Float;

    /// @brief The values of the parameter read as floats, one per component.
    std::vector<floatT> floats;

    /// @brief The values of the parameter read as ints, one per component.
    std::vector<intT> ints;

    /// @brief The values of the parameter read as strings, one per component.
    std::vector<String> strings;

    /// @brief The ramp of a ramp parameter, empty for every other kind.
    std::optional<prm::Ramp> ramp;
};

/**
 * @class CookSnapshot
 * @brief Everything one cook reads from the network, taken before the cook starts.
 *
 * A cook reads parameters, the frame, and the wiring of the network. All three
 * can change while a cook runs, so they are read once on the thread that owns
 * the network and handed to the cook as a fixed picture.
 *
 * Reading a fixed picture is what lets a cook run on several threads without
 * fighting the interface: the workers never read the live network, so the
 * interface is free to change it while they work.
 *
 * @note A snapshot is taken per cook. A new cook takes a new one, so an edit
 * made during a cook shows up in the cook that follows it.
 */
class CookSnapshot
{
  public:
    /**
     * @brief What one node reads while it cooks.
     */
    struct NodeState
    {
        /// @brief The node this state covers.
        nt::NodeId nodeId = nt::nullNode;

        /// @brief How many inputs the node takes.
        unsigned int inputCount = 0;

        /// @brief The parameters of the node, keyed by name.
        std::unordered_map<std::string, CookParameterValue> parameters;
    };

    /// @brief Sets the frame and the time the cook reads.
    void setTime(floatT frame, floatT time);

    /// @brief Returns the frame the cook reads.
    floatT frame() const { return frame_; }

    /// @brief Returns the time in seconds the cook reads.
    floatT time() const { return time_; }

    /// @brief Sets what one node reads while it cooks.
    void setNode(NodeState state);

    /// @brief Returns what one node reads while it cooks, or null when it covers no such node.
    const NodeState* findNode(nt::NodeId nodeId) const;

    /// @brief Sets one parameter's value for one node.
    void setParameter(nt::NodeId nodeId, const std::string& name, CookParameterValue value);

    /// @brief Returns one parameter's value, or null when the node has no such parameter.
    const CookParameterValue* findParameter(nt::NodeId nodeId, std::string_view name) const;

  private:
    floatT frame_ = 1;
    floatT time_ = 0;

    // What each node reads, keyed by node id.
    std::unordered_map<nt::NodeId, NodeState> nodes_;
};

} // namespace helix::async
