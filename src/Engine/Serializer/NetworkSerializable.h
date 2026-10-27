#include "Engine/Network/NodeSnapshot.h"
#include "Engine/Serializer/NodeLinkSerializable.h"
#include "Engine/Serializer/TimelineSerializable.h"
#include <cereal/types/vector.hpp>
#include <vector>

struct NetworkSerializable
{
    std::vector<helix::nt::NodeSnapshot> nodes;
    std::vector<NodeLinkSerializable> nodeLinks;
    TimelineSerializable timeline;

    template <class Archive> void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(nodes), CEREAL_NVP(nodeLinks), CEREAL_NVP(timeline));
    }
};
