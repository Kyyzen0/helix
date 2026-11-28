// Draws the ground grid: minor and major lines, and the two lines through the origin.
//
// The vertices are in cells from the centre of the grid, and the frame says how large a cell is
// (it follows the distance of the camera) and where the centre is, so the grid keeps readable
// cells at any zoom. The lines fade out with their distance from the centre.

#include "ShaderTypes.h"

using namespace metal;

struct GridVertexInput
{
    // x and z in cells, and the kind of the line.
    float3 position [[attribute(0)]];
};

struct GridVertexOutput
{
    float4 clipPosition [[position]];
    // The place of the vertex in cells from the centre of the grid.
    float2 cells;
    float kind [[flat]];
};

constant float minorOpacity = 0.32;
constant float majorOpacity = 0.60;
constant float axisOpacity = 0.90;
// The tints of the X and Z axes, mixed into the colour of the grid.
constant float3 xAxisTint = float3(0.86, 0.29, 0.29);
constant float3 zAxisTint = float3(0.30, 0.46, 0.92);

vertex GridVertexOutput gridVertex(
    GridVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    const float cell = frame.gridParams.x;
    const float2 centre = frame.gridParams.yz;
    const float kind = input.position.z;
    float2 world = centre + input.position.xy * cell;
    // The axes lie on the world axes, wherever the grid is centred.
    if (kind > 1.5 && kind < 2.5) world.y = 0.0;
    if (kind > 2.5) world.x = 0.0;

    GridVertexOutput output;
    output.clipPosition = frame.viewProjection * float4(world.x, 0.0, world.y, 1.0);
    output.cells = (world - centre) / cell;
    output.kind = kind;
    return output;
}

fragment float4 gridFragment(
    GridVertexOutput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    const float reach = length(input.cells) / frame.gridParams.w;
    const float fade = 1.0 - smoothstep(0.3, 1.0, reach);

    float3 color = frame.gridColor.rgb;
    float opacity = minorOpacity;
    if (input.kind > 0.5 && input.kind < 1.5)
    {
        opacity = majorOpacity;
    }
    else if (input.kind > 1.5 && input.kind < 2.5)
    {
        color = mix(color, xAxisTint, 0.75);
        opacity = axisOpacity;
    }
    else if (input.kind > 2.5)
    {
        color = mix(color, zAxisTint, 0.75);
        opacity = axisOpacity;
    }
    return float4(color, opacity * fade);
}
