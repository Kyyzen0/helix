#include "Engine/Core/Types.h"
#include "Engine/Script/PointScript.h"
#include "Engine/Script/ScriptContext.h"
#include "Engine/Script/ScriptEngine.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include "RunOverPoints.h"
#include <memory>
#include <string>

namespace {

class Script : public enzo::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

void Script::cook()
{
    using namespace enzo;

    if (!outputRequested(0)) return;

    const String code = evalParmString("code");

    const std::shared_ptr<const NodePacket> input = getInputPacket(0);
    NodePacket output = input->deepCopy();

    if (code.empty())
    {
        setOutputPacket(0, output);
        return;
    }

    String error;
    const std::shared_ptr<const script::PointScript> script =
        script::ScriptEngine::instance().compile(code, error);
    if (!script)
    {
        throwError(error);
        return;
    }

    // Shares one context across the cook, so a parameter the script reads is
    // evaluated and recorded as a dependency once.
    script::ScriptContext context(getNodeId(), *input);
    error = runOverPoints(*script, context, output);

    // Records what the script read even when it failed, so changing one of those
    // parameters cooks the node again.
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

ENZO_REGISTER_NODE(script, Script)
