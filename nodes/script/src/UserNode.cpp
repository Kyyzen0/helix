#include "Engine/Core/Types.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Script/PointScript.h"
#include "Engine/Script/ScriptContext.h"
#include "Engine/Script/ScriptEngine.h"
#include "RunOverPoints.h"
#include <memory>
#include <string>

/**
 * @brief A node whose behaviour comes from source the user wrote.
 *
 * A user node is a folder holding a node.yaml and the source the node runs. The
 * manifest gives the node its name, its label, its inputs and outputs, and the
 * parameters it shows. The source runs once per point of the geometry arriving
 * at the first input, the same way the script node's code does, so a user node
 * reads its parameters as `prm("name")` and edits the point's attributes as
 * `@name`.
 *
 * The source is compiled when the node is registered, so a node that does not
 * compile never reaches the tab menu and never stops the application from
 * starting. A source that fails while it runs leaves the node showing an error
 * and producing no geometry.
 */
namespace {

class UserNode : public enzo::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

void UserNode::cook()
{
    using namespace enzo;

    if (!outputRequested(0)) return;

    // The source the node was registered with, held against the node's folder.
    const std::shared_ptr<const script::PointScript> script =
        nt::NodeLoader::getUserScript(getNodeFolder());
    if (!script)
    {
        throwError("This user node's source is not available.");
        return;
    }

    const std::shared_ptr<const NodePacket> input = getInputPacket(0);
    NodePacket output = input->deepCopy();

    // Shares one context across the cook, so a parameter the source reads is
    // evaluated and recorded as a dependency once.
    script::ScriptContext context(getNodeId(), *input);
    String error = runOverPoints(*script, context, output);

    // Records what the source read even when it failed, so changing one of
    // those parameters cooks the node again.
    expr::submitExpressionDependencies(context);
    for (const String& warning : context.getWarnings()) throwWarning(warning);

    if (!error.empty())
    {
        throwError(error);
        return;
    }

    setOutputPacket(0, output);
}

} // namespace

ENZO_REGISTER_NODE(userNode, UserNode)
