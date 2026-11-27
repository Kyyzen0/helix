#pragma once
#include "Engine/Import/RawScene.h"
#include <filesystem>
#include <string>
#include <vector>

namespace helix::import {

/// @brief Reads a file with Assimp: glTF and GLB (Draco included), FBX, STL, PLY, Collada, 3DS,
/// 3MF, DXF, X3D and X.
/// @param[out] openedFiles When given, the other files Assimp opened to read this one (the .bin
/// of a glTF, say), so the caller can tell when one of them changes.
/// @throws ImportError when Assimp cannot read the file.
RawScene readWithAssimp(const std::filesystem::path& file, std::vector<std::filesystem::path>* openedFiles = nullptr);

/// @brief Reads a USD file (usd, usda, usdc, usdz) with Model I/O.
/// @throws ImportError when Model I/O cannot read the file.
RawScene readWithModelIO(const std::filesystem::path& file);

} // namespace helix::import
