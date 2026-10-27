#include "Engine/Core/Types.h"
#include <cereal/cereal.hpp>

struct TimelineSerializable
{
    helix::intT startFrame = 1;
    helix::intT endFrame = 240;
    helix::floatT fps = 24;
    helix::floatT frame = 1;

    template <class Archive> void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(startFrame), CEREAL_NVP(endFrame), CEREAL_NVP(fps), CEREAL_NVP(frame));
    }
};
