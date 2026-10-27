// Draws the ground grid, fading out with distance from the origin.

#include "ShaderTypes.h"

using namespace metal;

struct GridVertexInput
{
    float3 position [[attribute(0)]];
};

struct GridVertexOutput
{
    float4 clipPosition [[position]];
    float3 worldPosition;
};

constant float3 lineColor = float3(0.227, 0.227, 0.275);
constant float centerOpacity = 0.5;
constant float fadeDistance = 40.0;

vertex GridVertexOutput gridVertex(
    GridVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    GridVertexOutput output;
    output.clipPosition = frame.viewProjection * float4(input.position, 1.0);
    output.worldPosition = input.position;
    return output;
}

fragment float4 gridFragment(GridVertexOutput input [[stage_in]])
{
    float fade = saturate(1.0 - length(input.worldPosition) / fadeDistance);
    return float4(lineColor, centerOpacity * fade);
}
