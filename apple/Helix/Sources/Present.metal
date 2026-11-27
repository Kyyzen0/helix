#include <metal_stdlib>
using namespace metal;

// Draws one triangle that covers the whole target, with the texture of the engine's frame on it.

struct PresentVertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex PresentVertexOut presentVertex(uint vertexID [[vertex_id]])
{
    // The vertices (0, 0), (2, 0) and (0, 2) in uv, which cover the unit square.
    const float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    PresentVertexOut out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment float4 presentFragment(PresentVertexOut in [[stage_in]], texture2d<float> frame [[texture(0)]])
{
    constexpr sampler nearest(filter::nearest, address::clamp_to_edge);
    return float4(frame.sample(nearest, in.uv).rgb, 1.0);
}
