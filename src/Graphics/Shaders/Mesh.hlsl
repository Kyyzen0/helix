// Draws the display mesh lit by a key light and a fill light, and its wireframe over it.
//
// The colour of the mesh is the one the host asks for, so the lighting only shapes it.

struct VertexOutput
{
    float4 clipPosition : SV_Position;
    float3 normal : NORMAL;
    float3 worldPosition : WORLD_POSITION;
};

// The key light comes from above and to the side, the fill light from the other side.
static const float3 keyLightDirection = normalize(float3(0.6, 0.8, 0.4));
static const float3 fillLightDirection = normalize(float3(-0.5, 0.2, -0.7));

// The colour the faces are lit with, before the colour of the mesh is mixed in.
static const float3 keyLightColor = float3(1.0, 0.97, 0.92);
static const float3 fillLightColor = float3(0.55, 0.62, 0.75);
static const float3 ambientColor = float3(0.22, 0.23, 0.28);

// The wireframe is a darker shade of the mesh, so that it reads on any background.
static const float wireframeShade = 0.35;

VertexOutput vertexMain(float3 position : ATTRIB0, float3 normal : ATTRIB1)
{
    VertexOutput output;
    output.clipPosition = mul(viewProjection, float4(position, 1.0));
    output.normal = normal;
    output.worldPosition = position;
    return output;
}

// Lights the face with the key light, the fill light and the light of the sky.
float4 shadedPixelMain(VertexOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    if (!any(normal)) normal = float3(0.0, 1.0, 0.0);

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
    float3 color = geometryColor.rgb * light;
    return float4(saturate(color), 1.0);
}

float4 wireframePixelMain(VertexOutput input) : SV_Target
{
    return float4(geometryColor.rgb * wireframeShade, 1.0);
}
