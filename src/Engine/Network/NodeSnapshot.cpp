#include "Engine/Network/NodeSnapshot.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeTypeTable.h"

namespace helix {

nt::NodeSnapshot nt::NodeSnapshot::capture(nt::Node& node)
{
    NodeSnapshot snapshot;
    snapshot.typeName_ = node.getType().getFullName();
    snapshot.path_ = node.getPath();
    snapshot.position_ = node.getPosition();
    snapshot.aliasType_ = node.getAliasType();

    for (auto weakParameter : node.getParameters())
        if (auto parameter = weakParameter.lock())
            snapshot.parameters_.push_back(toSerializable(*parameter));

    return snapshot;
}

void nt::NodeSnapshot::restore(nt::NodeId nodeId) const { restore(nodeId, path_, position_); }

void nt::NodeSnapshot::restore(nt::NodeId nodeId, const Path& path, Vector2 position) const
{
    nm().createNodeWithId(nodeId, NodeTypeTable::requireNodeType(typeName_), path, position);

    nt::Node& node = nm().getNode(nodeId);
    node.setAliasType(aliasType_);
    applySerializable(node, parameters_);
}

} // namespace helix
