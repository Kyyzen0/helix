// Draws the display mesh lit by a key light and a fill light, and its wireframe over it.
//
// The colour of the mesh is the one the host asks for, so the lighting only shapes it.

#include "ShaderTypes.h"

using namespace metal;

struct MeshVertexInput
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
};

struct MeshVertexOutput
{
    float4 clipPosition [[position]];
    float3 normal;
};

// The colour the faces are lit with, before the colour of the mesh is mixed in.
constant float3 keyLightColor = float3(1.0, 0.97, 0.92);
constant float3 fillLightColor = float3(0.55, 0.62, 0.75);
constant float3 ambientColor = float3(0.22, 0.23, 0.28);

// The wireframe is a darker shade of the mesh, so that it reads on any background.
constant float wireframeShade = 0.35;

vertex MeshVertexOutput meshVertex(
    MeshVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    MeshVertexOutput output;
    output.clipPosition = frame.viewProjection * float4(input.position, 1.0);
    output.normal = input.normal;
    return output;
}

// Lights the face with the key light, the fill light and the light of the sky.
fragment float4 meshShadedFragment(
    MeshVertexOutput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    // The key light comes from above and to the side, the fill light from the other side.
    const float3 keyLightDirection = normalize(float3(0.6, 0.8, 0.4));
    const float3 fillLightDirection = normalize(float3(-0.5, 0.2, -0.7));

    float3 normal = normalize(input.normal);
    if (all(normal == float3(0.0))) normal = float3(0.0, 1.0, 0.0);

    // The key light falls off towards the faces that are turned away from it.
    float key = saturate(dot(normal, keyLightDirection));
    float fill = saturate(dot(normal, fillLightDirection));

    // The light of the sky reaches the faces that look up, so that a shape stays readable
    // from below as well.
    float sky = normal.y * 0.5 + 0.5;

    float3 light = ambientColor + keyLightColor * key + fillLightColor * fill * 0.35 +
                   ambientColor * sky * 0.6;

    // The colour of the mesh is mixed into the light, so that the mesh keeps the colour
    // the host asks for.
    float3 color = frame.geometryColor.rgb * light;
    return float4(saturate(color), 1.0);
}

fragment float4 meshWireframeFragment(
    MeshVertexOutput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    return float4(frame.geometryColor.rgb * wireframeShade, 1.0);
}
