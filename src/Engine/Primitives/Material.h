#pragma once
#include "Engine/Core/Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace helix::geo {

/**
 * @brief A picture a material uses, decoded to pixels.
 *
 * Immutable once built: it is shared through `std::shared_ptr<const Image>` by every material
 * that uses it, and by every copy of a mesh.
 *
 * @par Conventions
 * - 8 bits per channel, four channels (red, green, blue, alpha), `width * height * 4` bytes.
 * - sRGB encoded, alpha not premultiplied. A shader that samples it through an sRGB texture
 *   format gets linear values.
 * - Rows run from the top of the picture to the bottom.
 * - The `uv` attribute of a mesh has its origin at the bottom left, v pointing up, which is what
 *   Assimp (every format it reads, glTF included, whose own origin is the top left), Model I/O and
 *   the obj reader all give. The texel for (u, v) is therefore at column `u * width` and row
 *   `(1 - v) * height` counted from the top. @ref sampleNearest does exactly that.
 */
struct Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// @brief 8 bit RGBA, sRGB encoded, rows top to bottom.
    std::vector<std::uint8_t> rgba;
    /// @brief Where the picture came from: a file path, or "<file>#embedded:N" for a picture
    /// kept inside the model file. Ends with " (scaled down from WxH)" when it was reduced.
    std::string source;

    std::size_t byteSize() const { return rgba.size(); }

    /// @brief The pixel at a column and a row from the top, clamped to the picture.
    std::array<std::uint8_t, 4> pixel(std::uint32_t x, std::uint32_t y) const
    {
        if (width == 0 || height == 0 || rgba.size() < std::size_t(width) * height * 4) return {0, 0, 0, 0};
        x = std::min(x, width - 1);
        y = std::min(y, height - 1);
        const std::uint8_t* p = &rgba[(std::size_t(y) * width + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }

    /// @brief The texel a `uv` attribute value (u, v) designates, without filtering and with the
    /// coordinates clamped to the picture. See the conventions above.
    std::array<std::uint8_t, 4> sampleNearest(float u, float v) const
    {
        const float cu = std::clamp(u, 0.f, 1.f);
        const float cv = std::clamp(v, 0.f, 1.f);
        const auto column = static_cast<std::uint32_t>(std::min(cu * float(width), float(width) - 1.f));
        const auto row = static_cast<std::uint32_t>(std::min((1.f - cv) * float(height), float(height) - 1.f));
        return pixel(column, row);
    }
};

/**
 * @brief What a face is made of, as the file described it. Immutable once built, shared by
 * `std::shared_ptr<const Material>` between every mesh that uses it.
 *
 * A mesh holds a table of materials and a `material_index` face attribute that points into it
 * (see Mesh::materials). Only the base colour is kept: no other map is read yet.
 */
struct Material
{
    std::string name;
    /// @brief The base colour factor, linear, each channel from 0 to 1. It multiplies the texture.
    Vector4 baseColor{1.f, 1.f, 1.f, 1.f};
    /// @brief The base colour picture, or null when the material has none (or it did not load).
    std::shared_ptr<const Image> baseColorTexture;
    /// @brief Empty, or what went wrong with a texture the file referenced and the import could
    /// not load: which one, and why. The material keeps its factor.
    std::string missingTexture;
};

} // namespace helix::geo
