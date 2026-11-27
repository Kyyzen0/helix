#pragma once
#include "Engine/Import/RawScene.h"
#include <filesystem>
#include <string>

namespace helix::import {

/// @brief Reads a file with Assimp: glTF and GLB (Draco included), FBX, STL, PLY, Collada, 3DS,
/// 3MF, DXF, X3D and X.
/// @throws ImportError when Assimp cannot read the file.
RawScene readWithAssimp(const std::filesystem::path& file);

/// @brief Reads a USD file (usd, usda, usdc, usdz) with Model I/O.
/// @throws ImportError when Model I/O cannot read the file.
RawScene readWithModelIO(const std::filesystem::path& file);

} // namespace helix::import
