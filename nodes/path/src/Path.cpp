#include "Engine/Core/Types.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeRegistry.h"

namespace {

class Path : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

void Path::cook()
{
    using namespace helix;

    if (!outputRequested(0)) return;

    NodePacket packet = cloneInputPacket(0);

    String path = evalParmString("path");

    for (unsigned int p = 0; p < packet.size(); ++p)
    {
        packet.getPrimitive(p)->setPath(path);
    }

    setOutputPacket(0, packet);
}

} // namespace

HELIX_REGISTER_NODE(path, Path)
