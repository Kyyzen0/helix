// Draws each camera as an orange frame placed by its transform.

#include "ShaderTypes.h"

using namespace metal;

struct CameraPrimVertexInput
{
    float3 position [[attribute(0)]];
    // The transform of the camera, one column per attribute.
    float4 transformColumn0 [[attribute(1)]];
    float4 transformColumn1 [[attribute(2)]];
    float4 transformColumn2 [[attribute(3)]];
    float4 transformColumn3 [[attribute(4)]];
};

constant float3 frameColor = float3(0.9, 0.7, 0.2);

vertex float4 cameraPrimVertex(
    CameraPrimVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    float4 worldPosition = input.transformColumn0 * input.position.x +
                           input.transformColumn1 * input.position.y +
                           input.transformColumn2 * input.position.z + input.transformColumn3;
    return frame.viewProjection * worldPosition;
}

fragment float4 cameraPrimFragment()
{
    return float4(frameColor, 1.0);
}
