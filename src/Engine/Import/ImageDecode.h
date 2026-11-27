#pragma once
#include "Engine/Primitives/Material.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

struct CGImage;

namespace helix::import {

/// @brief The longest side a texture keeps. A bigger picture is scaled down to fit.
inline constexpr std::uint32_t kMaxTextureSide = 8192;

/// @brief A decoded picture, or why there is none.
struct DecodeResult
{
    std::shared_ptr<const geo::Image> image;
    /// @brief Set when image is null: one sentence for a person.
    std::string error;
};

/**
 * @brief Decodes a picture (PNG, JPEG, HEIC, TIFF, ... whatever ImageIO reads) to 8 bit sRGB RGBA.
 *
 * Rows come out top to bottom and the alpha is not premultiplied (see geo::Image). A picture whose
 * longest side is over kMaxTextureSide is scaled down to fit, and `source` of the result says so.
 * @param source What to call the picture: a path, or "<file>#embedded:N".
 */
DecodeResult decodeImage(const void* bytes, std::size_t size, const std::string& source);

/// @brief Reads a file and decodes it like @ref decodeImage, with the path as the source.
DecodeResult decodeImageFile(const std::filesystem::path& file);

/// @brief Converts a CGImage (which Model I/O hands out) like @ref decodeImage.
DecodeResult decodeCGImage(CGImage* image, const std::string& source);

} // namespace helix::import
