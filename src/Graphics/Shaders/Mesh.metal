// Draws the display mesh lit so that its volume reads, and its wireframe over it.
//
// The lights follow the camera, as in the viewport of a modelling tool: the main light comes
// from above and to the left of the viewer, a fill light from the right and below, and a rim
// light from behind lifts the silhouette off the background. A hemisphere term (sky above,
// ground below, in world space) keeps the faces that turn away from every light readable, and the
// same sky and ground are mirrored faintly in the surface, so a dark material still has an edge.
//
// The colour the host asks for is a display (sRGB) colour. The lighting is computed in linear
// light and the result is encoded back, so the shading is neither washed out nor crushed.

#include "ShaderTypes.h"

using namespace metal;

struct MeshVertexInput
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct MeshVertexOutput
{
    float4 clipPosition [[position]];
    float3 worldPosition;
    float3 normal;
    float2 uv;
};

// The lights, in the space of the camera: x to the right, y up, z toward the viewer.
constant float3 keyLightDirection = float3(-0.60, 0.60, 0.52);
constant float3 fillLightDirection = float3(0.70, -0.20, 0.45);
constant float3 keyLightColor = float3(1.0, 0.96, 0.90);
constant float3 fillLightColor = float3(0.50, 0.55, 0.64);
constant float3 rimLightColor = float3(0.62, 0.74, 1.0);

// The light of the sky and of the ground, in linear light.
// Soft enough that a light surface does not wash out, and bright enough that a dark one still
// shows its shape: the ambient is a gradient, so a face is lit by where it points.
constant float3 skyColor = float3(0.30, 0.34, 0.42);
constant float3 groundColor = float3(0.11, 0.10, 0.09);

// How much of the sky and the ground a surface mirrors at a grazing angle, and head on (4% for a
// dielectric). It is what gives a very dark material, which no ambient light can lift, an edge
// and a sheen to read it by.
constant float environmentReflection = 0.9;
constant float reflectanceHeadOn = 0.04;

// The wireframe is a darker shade of the mesh, so that it reads on any background.
constant float wireframeShade = 0.35;

float3 srgbToLinear(float3 color)
{
    return select(pow((color + 0.055) / 1.055, 2.4), color / 12.92, color <= 0.04045);
}

float3 linearToSrgb(float3 color)
{
    return select(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, color <= 0.0031308);
}

vertex MeshVertexOutput meshVertex(
    MeshVertexInput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    MeshVertexOutput output;
    output.clipPosition = frame.viewProjection * float4(input.position, 1.0);
    output.worldPosition = input.position;
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

// Lights the face with the key, fill, rim and hemisphere lights.
//
// The albedo is the base colour factor times the base colour texture when the draw has a material,
// and the colour of the frame otherwise. The texture is an sRGB texture, so it is sampled as linear
// light. The origin of the uv is the bottom left of the picture while the first row of a texture is
// its top, hence the flip of v. The alpha of the material is ignored.
fragment float4 meshShadedFragment(
    MeshVertexOutput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]],
    constant MaterialConstants& material [[buffer(HELIX_MATERIAL_CONSTANTS_SLOT)]],
    texture2d<float> baseColorTexture [[texture(HELIX_BASE_COLOR_TEXTURE_SLOT)]],
    sampler baseColorSampler [[sampler(HELIX_BASE_COLOR_SAMPLER_SLOT)]]
)
{
    float3 normal = normalize(input.normal);
    if (all(normal == float3(0.0))) normal = float3(0.0, 1.0, 0.0);

    // The camera looks along -forward; its basis carries the lights with it.
    const float3 right = frame.cameraRight.xyz;
    const float3 up = frame.cameraUp.xyz;
    const float3 toViewer = cross(right, up);
    const float3 view = normalize(frame.cameraPosition.xyz - input.worldPosition);

    // A face seen from behind (an open surface, a flipped normal) is lit as the side facing
    // the viewer, rather than left black.
    if (dot(normal, view) < 0.0) normal = -normal;

    const float3 keyDirection = normalize(
        right * keyLightDirection.x + up * keyLightDirection.y + toViewer * keyLightDirection.z
    );
    const float3 fillDirection = normalize(
        right * fillLightDirection.x + up * fillLightDirection.y + toViewer * fillLightDirection.z
    );

    // The key light is wrapped a little, so the shadow side falls off softly instead of
    // cutting to black at the terminator.
    const float key = saturate((dot(normal, keyDirection) + 0.15) / 1.15);
    const float fill = saturate(dot(normal, fillDirection));

    // Sky above and ground below, from the world up axis.
    const float3 hemisphere = mix(groundColor, skyColor, normal.y * 0.5 + 0.5);

    // The edge of the shape facing away from the viewer picks up the rim light, more on the
    // side the key light does not reach, so it separates the silhouette from the background.
    const float facing = 1.0 - saturate(dot(normal, view));
    const float rim = pow(facing, 3.0) * (0.55 + 0.45 * saturate(1.0 - key));

    float3 albedo = srgbToLinear(frame.geometryColor.rgb);
    if (material.useMaterial != 0)
    {
        albedo = material.baseColor.rgb;
        if (material.hasTexture != 0)
            albedo *= baseColorTexture.sample(baseColorSampler, float2(input.uv.x, 1.0 - input.uv.y)).rgb;
    }
    // Every light is tinted by the colour of the mesh, so the mesh keeps the colour the host
    // asked for: a pure red mesh stays pure red, and only its brightness changes.
    const float3 halfVector = normalize(keyDirection + view);
    const float highlight = pow(saturate(dot(normal, halfVector)), 40.0) * 0.10;
    float3 light = hemisphere + keyLightColor * key * 0.95 + fillLightColor * fill * 0.25 +
                   rimLightColor * rim * 0.8 + keyLightColor * highlight;
    float3 color = albedo * light;

    // The environment: the sky and the ground seen in the surface, stronger toward the edge.
    const float3 reflected = reflect(-view, normal);
    const float3 environment = mix(groundColor, skyColor, reflected.y * 0.5 + 0.5);
    const float fresnel = reflectanceHeadOn + (1.0 - reflectanceHeadOn) * pow(1.0 - saturate(dot(normal, view)), 5.0);
    // Tinted by the hue of the albedo, so a pure red mesh stays pure red: the reflection lifts a
    // dark material without turning it grey.
    const float3 hue = saturate(albedo / max(max(albedo.r, max(albedo.g, albedo.b)), 0.04));
    color += environment * fresnel * environmentReflection * hue;

    return float4(saturate(linearToSrgb(saturate(color))), 1.0);
}

fragment float4 meshWireframeFragment(
    MeshVertexOutput input [[stage_in]],
    constant FrameConstants& frame [[buffer(HELIX_FRAME_CONSTANTS_SLOT)]]
)
{
    return float4(frame.geometryColor.rgb * wireframeShade, frame.wireframeOpacity);
}
