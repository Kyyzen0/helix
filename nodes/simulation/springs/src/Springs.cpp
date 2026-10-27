#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/SimulationSettings.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace {

using helix::floatT;
using helix::intT;

/**
 * @brief A sheet of points joined by springs, hanging over time.
 *
 * The sheet is always worked out from the frame the node starts on, so the
 * state at a frame is the same whether the scene was played to it or scrubbed
 * to it.
 *
 * Every setting can be keyframed from the timeline. A setting that holds
 * keyframes is read at each step of the simulation, so the sheet moves with
 * the animation rather than jumping when the frame changes.
 */
class Springs : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

void Springs::cook()
{
    using namespace helix;

    // Reading the frame tells the engine the node depends on the time, so it
    // cooks again whenever the scene moves.
    const floatT frame = getFrame();

    const intT startFrame = std::max<intT>(1, evalParmInt("start_frame"));
    const intT subSteps = std::max<intT>(1, evalParmInt("sub_steps"));
    const floatT fps = nt::nm().getFps();
    const floatT firstFrame = static_cast<floatT>(startFrame);

    if (!outputRequested(0)) return;

    NodePacket packet;
    auto mesh = std::make_shared<geo::Mesh>();

    const auto settingsAtFrame = [this](floatT atFrame) {
        return sim::springSettingsAt(getNodeId(), atFrame);
    };

    const std::vector<Vector3> positions = sim::simulateSprings(
        sim::springSettingsAt(getNodeId(), firstFrame),
        settingsAtFrame,
        firstFrame,
        frame,
        fps,
        subSteps
    );

    const std::vector<Offset> offsets = mesh->addPoints(positions);

    // The sheet is drawn as a grid of faces, so it covers the gaps between its
    // points. The shape of the grid is the shape the sheet was built with, not
    // the one it has settled into.
    const intT columns = std::max<intT>(1, evalParmInt("columns"));
    const intT rows = std::max<intT>(1, evalParmInt("rows"));
    if (static_cast<intT>(offsets.size()) >= columns * rows)
    {
        for (intT row = 0; row + 1 < rows; ++row)
        {
            for (intT column = 0; column + 1 < columns; ++column)
            {
                const Offset topLeft = offsets[static_cast<std::size_t>(row * columns + column)];
                const Offset topRight = offsets[static_cast<std::size_t>(row * columns + column + 1)];
                const Offset bottomLeft =
                    offsets[static_cast<std::size_t>((row + 1) * columns + column)];
                const Offset bottomRight =
                    offsets[static_cast<std::size_t>((row + 1) * columns + column + 1)];
                mesh->addFace({topLeft, topRight, bottomRight, bottomLeft}, true);
            }
        }
    }

    packet.addPrimitive(std::move(mesh));
    setOutputPacket(0, packet);
}

} // namespace

HELIX_REGISTER_NODE(springs, Springs)
