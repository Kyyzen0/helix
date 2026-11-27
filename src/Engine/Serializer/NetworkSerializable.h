#include "Engine/Network/NodeSnapshot.h"
#include "Engine/Serializer/NodeLinkSerializable.h"
#include "Engine/Serializer/TimelineSerializable.h"
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <vector>

struct NetworkSerializable
{
    std::vector<helix::nt::NodeSnapshot> nodes;
    std::vector<NodeLinkSerializable> nodeLinks;
    TimelineSerializable timeline;

    /// @brief The index in `nodes` of the node the viewport shows, or -1 when none is shown.
    int displayNodeIndex = -1;
    /// @brief Whether the file stored a display node at all. A file written before the display
    /// node was saved does not, which is different from a file that stores "none".
    bool hasDisplayNodeIndex = false;

    template <class Archive> void save(Archive& ar) const
    {
        ar(CEREAL_NVP(nodes), CEREAL_NVP(nodeLinks), CEREAL_NVP(timeline));
        ar(CEREAL_NVP(displayNodeIndex));
    }

    template <class Archive> void load(Archive& ar)
    {
        ar(CEREAL_NVP(nodes), CEREAL_NVP(nodeLinks), CEREAL_NVP(timeline));

        // Scenes saved before the display node was stored have no such field, and still open.
        // The field comes last, so a missing one leaves the archive where it was.
        try
        {
            ar(CEREAL_NVP(displayNodeIndex));
            hasDisplayNodeIndex = true;
        }
        catch (const cereal::Exception&)
        {
            displayNodeIndex = -1;
            hasDisplayNodeIndex = false;
        }
    }
};
