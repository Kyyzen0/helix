#pragma once

/**
 * @file ImageFile.h
 * @brief Writes a rendered frame to a PNG file. Internal to the facade.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace helix::core::detail {

/// @brief Writes RGBA8 rows from the top as an opaque sRGB PNG.
///
/// The PNG is written next to the destination and renamed over it, so a failed write leaves
/// the previous file whole.
/// @param rgba four bytes per pixel, `width * height * 4` of them in all.
/// @return An empty string on success, otherwise why the file could not be written.
std::string writePng(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba
);

} // namespace helix::core::detail
