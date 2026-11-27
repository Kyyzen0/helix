#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Network/CookCache.h"
#include "Engine/Network/CookContext.h"
#include "Engine/Network/NodePacket.h"
#include <boost/config.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helix::prm {
class Ramp;
}

namespace helix::nt {
class Node;

/**
 * @brief The base class a node author subclasses to write a node's behaviour.
 *
 * One of these is built for every cook and thrown away when the cook ends, so
 * anything it stores is scoped to that cook and cannot leak into the next one.
 * Cross cook state belongs on nt::Node instead, where the engine can invalidate
 * it.
 *
 * Example
 *
 * ```
 * class Circle : public nt::NodeImpl
 * {
 *     using NodeImpl::NodeImpl;
 *     void cook() override { setOutputPacket(0, buildMesh()); }
 * };
 * HELIX_REGISTER_NODE(circle, Circle)
 * ```
 *
 * @note Every cook time query lives here as a forwarder, so an author writes
 * evalParmFloat("radius") and never names the context behind it.
 */
class BOOST_SYMBOL_EXPORT NodeImpl
{
  public:
    NodeImpl(Node& node, CookContext& context) : node_{node}, context_{context} {}
    virtual ~NodeImpl() = default;

    /// @brief Produces this node's output packets from its inputs and parameters.
    /// @post Any output left unset stays an empty packet.
    virtual void cook() = 0;

  protected:
    /// @brief Records an error on the node, for a person or an agent to read (nt::Node::getError).
    /// @note It does not stop the cook: it returns, and the node has to return after it, so the
    /// outputs it did not set stay empty packets. Only the first error of a cook is kept.
    /// Throwing an exception does the same, and the engine records its message.
    void throwError(std::string error);

    /// @brief Records a warning on the node without interrupting the cook.
    void throwWarning(std::string warning);

    /// @brief Returns the id of the node being cooked.
    NodeId getNodeId() const;

    /// @brief Returns the folder the node was loaded from, where its assets live.
    const std::filesystem::path& getNodeFolder() const;

    /// @brief Returns whether anything downstream is asking for this output.
    /// @todo Always true until nt::Node tracks dirtiness per output.
    bool outputRequested(unsigned int outputIndex);

    /// @brief Hands finished geometry to one of the node's outputs.
    /// @todo std::move the geometry instead of copying it.
    void setOutputPacket(unsigned int outputIndex, helix::NodePacket packet);

    /// @brief Returns a writable copy of the geometry arriving at one input.
    helix::NodePacket cloneInputPacket(unsigned int inputIndex);

    /// @brief Returns the geometry arriving at an input, read only.
    std::shared_ptr<const helix::NodePacket> getInputPacket(unsigned int inputIndex);

    /// @brief Returns whether an input has anything connected to it.
    bool hasInput(unsigned int inputIndex);

    /// @brief Returns how many inputs the node takes.
    /// @note A multi input port holds one input per node link it has.
    unsigned int getInputCount();

    /// @brief Returns the frame the scene sits on.
    /// @note The frame can be fractional.
    floatT getFrame() const;

    /// @brief Returns the scene time in seconds, measured from the start of frame 1.
    floatT getTime() const;

    floatT evalParmFloat(std::string_view parmName, const unsigned int index = 0) const;
    intT evalParmInt(std::string_view parmName, const unsigned int index = 0) const;
    boolT evalParmBool(std::string_view parmName, const unsigned int index = 0) const;
    String evalParmString(std::string_view parmName, const unsigned int index = 0) const;

    // Read every value of a multi value parameter at once, where the singular
    // forms above read one value by index.
    std::vector<floatT> evalParmFloats(std::string_view parmName) const;
    std::vector<intT> evalParmInts(std::string_view parmName) const;
    std::vector<String> evalParmStrings(std::string_view parmName) const;

    Vector2 evalParmVector2(std::string_view parmName) const;
    Vector3 evalParmVector3(std::string_view parmName) const;

    /// @brief Snapshots a ramp parameter into a sampler for use during the cook.
    helix::prm::Ramp evalParmRamp(std::string_view parmName) const;

    /**
     * @brief Returns the state a solver of this node saved, at the highest whole frame not above
     * `maxIndex`, or nothing.
     *
     * A solver that is always worked out from its first frame can resume from the state an earlier
     * cook left behind instead. The states are filed in the cook cache (see nt::CookCache) under
     * everything this cook depends on except the frame, so a state is only found by a cook that
     * would have reached it itself: any change to a parameter, an input or a keyframe of the
     * node loses the states. Nothing is found when the cache is off, and for a cook on a worker
     * thread, which does not use the cache.
     *
     * @param slot Tells the solvers of one node apart, when it has several.
     * @param maxIndex The most whole frames after the first frame the caller wants a state at.
     */
    std::optional<CachedState> findSolverState(unsigned int slot, std::int64_t maxIndex) const;

    /// @brief Saves the state of a solver after `index` whole frames, see findSolverState.
    /// @param bytes The estimated size of the state, which the cache budget counts.
    void saveSolverState(
        unsigned int slot,
        std::int64_t index,
        std::shared_ptr<const void> state,
        std::size_t bytes
    ) const;

  private:
    Node& node_;
    CookContext& context_;
};

} // namespace helix::nt
