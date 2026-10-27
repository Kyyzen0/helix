#pragma once

#include "Engine/Core/Types.h"
#include "Engine/Network/NodePacket.h"

namespace helix::nt {
class NetworkManager;
}

namespace helix::prm {
class Ramp;
}

namespace helix::async {
// The pictures a cook reads from, defined in the Async folder.
struct CookParameterValue;
class CookSnapshot;
class CookPlan;
class CookResult;
class CookCancelToken;
} // namespace helix::async

namespace helix::nt {
/**
 * @class CookContext
 * @brief The handle a node holds while it cooks to get context about the network.
 *
 * Carries the runtime context a cook reads from the network, covering parameter values, input
 * geometry and the scene time.
 *
 * @note A context comes in two kinds. The ordinary one reads the live network, and it is the one
 * the single threaded cook uses. The other reads a snapshot taken before the cook started, and it
 * is the one a cook on a worker thread uses. The snapshot kind never reads the network, so a cook
 * using it touches no shared engine state and several of them can run at once.
 */
class CookContext
{
  public:
    /**
     * @brief Builds a context that reads the live network.
     *
     * @param nodeId The node being cooked.
     * @param networkManager The network the cook reads from.
     */
    CookContext(helix::nt::NodeId nodeId, helix::nt::NetworkManager& networkManager);

    /**
     * @brief Builds a context that reads a snapshot rather than the live network.
     *
     * @param nodeId The node being cooked.
     * @param networkManager The network the cook belongs to.
     * @param snapshot The values the cook reads, taken before the cook started.
     * @param plan The wiring the cook reads, taken before the cook started.
     * @param result The geometry the cook produces and reads back.
     * @param cancelToken The flag that tells the cook it has been abandoned.
     */
    CookContext(
        helix::nt::NodeId nodeId,
        helix::nt::NetworkManager& networkManager,
        const helix::async::CookSnapshot& snapshot,
        const helix::async::CookPlan& plan,
        helix::async::CookResult& result,
        const helix::async::CookCancelToken& cancelToken
    );

    /// @brief Returns the id of the node being cooked.
    helix::nt::NodeId getNodeId() const { return nodeId_; }
    helix::NodePacket cloneInputPacket(unsigned int inputIndex);

    /// @brief Returns the geometry arriving at an input, read only.
    std::shared_ptr<const helix::NodePacket> getInputPacket(unsigned int inputIndex);
    bool hasInput(unsigned int inputIndex);
    unsigned int getInputCount();

    /// @brief Returns whether the cook has been abandoned and should stop.
    bool isCancelled() const;

    /// @brief Returns the frame the scene sits on.
    /// @note The frame can be fractional.
    floatT getFrame() const;

    /// @brief Returns the scene time in seconds, measured from the start of frame 1.
    floatT getTime() const;

    floatT evalParmFloat(std::string_view parmName, const unsigned int index = 0) const;
    intT evalParmInt(std::string_view parmName, const unsigned int index = 0) const;
    boolT evalParmBool(std::string_view parmName, const unsigned int index = 0) const;
    String evalParmString(std::string_view parmName, const unsigned int index = 0) const;

    // Read every value of a parameter as a list. The singular forms above read
    // one value by index, these read them all at once.
    std::vector<floatT> evalParmFloats(std::string_view parmName) const;
    std::vector<intT> evalParmInts(std::string_view parmName) const;
    std::vector<String> evalParmStrings(std::string_view parmName) const;

    // Read the first two float components of a parameter as a Vector2. Missing
    // components default to zero.
    Vector2 evalParmVector2(std::string_view parmName) const;

    // Read the first three float components of a parameter as a Vector3. Missing
    // components default to zero.
    Vector3 evalParmVector3(std::string_view parmName) const;

    // Snapshot a ramp parameter into a sampler for use during cook.
    helix::prm::Ramp evalParmRamp(std::string_view parmName) const;

  private:
    /// @brief Records that the cooking node reads the scene time.
    void recordTimeDependency_() const;

    /// @brief Returns the snapshot's value for a parameter, or null when it holds none.
    const helix::async::CookParameterValue* findParameterValue_(
        std::string_view parmName
    ) const;

    /// @brief Returns the snapshot's value for one component of a parameter.
    floatT snapshotFloat_(const helix::async::CookParameterValue& value, unsigned int index) const;
    intT snapshotInt_(const helix::async::CookParameterValue& value, unsigned int index) const;
    String snapshotString_(const helix::async::CookParameterValue& value, unsigned int index) const;

    helix::nt::NodeId nodeId_;
    helix::nt::NetworkManager& networkManager_;

    // Set when the context reads a snapshot rather than the live network. The
    // pointers are null for the ordinary kind.
    const helix::async::CookSnapshot* snapshot_ = nullptr;
    const helix::async::CookPlan* plan_ = nullptr;
    helix::async::CookResult* result_ = nullptr;
    const helix::async::CookCancelToken* cancelToken_ = nullptr;
};
} // namespace helix::nt
