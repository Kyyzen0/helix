// Draws each point as a white disc flat to the screen. The whole disc takes the depth of its
// centre, so a surface the point sits on never cuts through it.

#include "ShaderTypes.h"

using namespace metal;

struct PointVertexInput
{
    float2 corner [[attribute(0)]];
    float3 pointPosition [[attribute(1)]];
};

struct PointVertexOutput
{
    float4 clipPosition [[position]];
    float2 corner;
};

// Diameter of a disc in interface pixels.
constant float discDiameter = 3.5;

// Fraction of the distance to the camera a disc is moved toward it, so a point on a face draws over it.
constant float cameraPull = 0.01;

vertex PointVertexOutput pointsVertex(
    PointVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    float3 pulledPosition = mix(input.pointPosition, frame.cameraPosition.xyz, cameraPull);
    float4 centreClipPosition = frame.viewProjection * float4(pulledPosition, 1.0);

    // Converts the corner from pixels to clip units, which span 2 across the viewport.
    float2 cornerPixels = input.corner * discDiameter * frame.pixelRatio;
    float2 cornerClipOffset = cornerPixels * 2.0 / frame.viewportPixelSize * centreClipPosition.w;

    PointVertexOutput output;
    output.clipPosition = centreClipPosition + float4(cornerClipOffset, 0.0, 0.0);
    output.corner = input.corner;
    return output;
}

fragment float4 pointsFragment(PointVertexOutput input [[stage_in]])
{
    if (length(input.corner) > 0.5) discard_fragment();
    return float4(1.0, 1.0, 1.0, 1.0);
}
