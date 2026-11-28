#pragma once

namespace helix::gfx {

/// @brief How many cells the ground grid holds on each side of its centre.
inline constexpr int gridHalfCells = 50;

/// @brief How many cells lie between two major lines of the ground grid.
inline constexpr int gridMajorEvery = 10;

/// @brief Returns the side of a cell of the ground grid, in scene units (metres), for a camera at
/// a distance from what it looks at.
///
/// The side is a 1, 2 or 5 times a power of ten, the smallest one that is at least a tenth of the
/// distance, so the camera is always four to ten cells from what it looks at (the view
/// holds about ten to twenty cells across) and a cell never shrinks to a line or grows past the screen. A distance that is not a positive number
/// gives 1.
float chooseGridCellSize(float cameraDistance);

} // namespace helix::gfx
