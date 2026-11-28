// Declares what the C++ renderer and the Metal shaders both read, so the two
// sides cannot drift apart. The C++ side includes it for the layout of the
// frame constants, and every .metal file includes it for the same struct.
#pragma once

#ifdef __METAL_VERSION__
#include <metal_stdlib>
#define HELIX_SHADER_MAT4 metal::float4x4
#define HELIX_SHADER_VEC4 metal::float4
#define HELIX_SHADER_VEC2 metal::float2
#define HELIX_SHADER_UINT uint
#else
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#define HELIX_SHADER_MAT4 glm::mat4
#define HELIX_SHADER_VEC4 glm::vec4
#define HELIX_SHADER_VEC2 glm::vec2
#define HELIX_SHADER_UINT std::uint32_t
#endif

// The slot of the argument table the frame constants are bound to, in the vertex
// and the fragment stage. The vertex buffers take the slots below it.
#define HELIX_FRAME_CONSTANTS_SLOT 10

// The slot the fragment stage reads the material of a draw from, and the texture and the sampler
// slots of the base colour.
#define HELIX_MATERIAL_CONSTANTS_SLOT 11
#define HELIX_BASE_COLOR_TEXTURE_SLOT 0
#define HELIX_BASE_COLOR_SAMPLER_SLOT 0

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
    /// The colour of the ground grid lines, in the colour space of the shader constants of `lineColor`.
    HELIX_SHADER_VEC4 gridColor;
    /// The unit vectors of the camera in world space, which the lights of the mesh follow.
    HELIX_SHADER_VEC4 cameraRight;
    HELIX_SHADER_VEC4 cameraUp;
    HELIX_SHADER_VEC2 viewportPixelSize;
    float pixelRatio;
    /// How opaque the wireframe is, from 0 to 1.
    float wireframeOpacity;
    /// The ground grid: x is the side of a cell, y and z the world x and z of its centre (a multiple
    /// of a major cell, so the lines stay where they are in the world), w the cells on each side.
    HELIX_SHADER_VEC4 gridParams;
};

/// What one draw of the mesh is shaded with, set per batch of faces.
struct MaterialConstants
{
    /// The base colour factor, in linear light. It multiplies the texture.
    HELIX_SHADER_VEC4 baseColor;
    /// 1 to shade with the base colour and the texture, 0 to shade with the colour of the frame.
    HELIX_SHADER_UINT useMaterial;
    /// 1 when a base colour texture is bound, 0 when the factor alone is used.
    HELIX_SHADER_UINT hasTexture;
    HELIX_SHADER_UINT padding0;
    HELIX_SHADER_UINT padding1;
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FrameConstants) == 176, "FrameConstants must match the layout the shaders read");
static_assert(sizeof(MaterialConstants) == 32, "MaterialConstants must match the layout the shaders read");
} // namespace helix::gfx
#endif
