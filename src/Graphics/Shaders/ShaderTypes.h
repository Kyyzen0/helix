// Declares what the C++ renderer and the Metal shaders both read, so the two
// sides cannot drift apart. The C++ side includes it for the layout of the
// frame constants, and every .metal file includes it for the same struct.
#pragma once

#ifdef __METAL_VERSION__
#include <metal_stdlib>
#define HELIX_SHADER_MAT4 metal::float4x4
#define HELIX_SHADER_VEC4 metal::float4
#define HELIX_SHADER_VEC2 metal::float2
#else
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#define HELIX_SHADER_MAT4 glm::mat4
#define HELIX_SHADER_VEC4 glm::vec4
#define HELIX_SHADER_VEC2 glm::vec2
#endif

// The slot of the argument table the frame constants are bound to, in the vertex
// and the fragment stage. The vertex buffers take the slots below it.
#define HELIX_FRAME_CONSTANTS_SLOT 10

#ifndef __METAL_VERSION__
namespace helix::gfx {
#endif

/// The values every pass reads. Both sides lay the fields out the same way: a
/// column major matrix, then 16 byte vectors, then the 8 byte vector and two floats.
struct FrameConstants
{
    HELIX_SHADER_MAT4 viewProjection;
    HELIX_SHADER_VEC4 cameraPosition;
    HELIX_SHADER_VEC4 geometryColor;
    HELIX_SHADER_VEC2 viewportPixelSize;
    float pixelRatio;
    float padding;
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FrameConstants) == 112, "FrameConstants must match the layout the shaders read");
} // namespace helix::gfx
#endif
