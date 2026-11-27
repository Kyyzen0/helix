#pragma once
#include "Engine/Import/ImportFile.h"
#include "Engine/Import/RawScene.h"

namespace helix::import {

/**
 * @brief Turns what a reader found into engine meshes.
 *
 * The transform of every instance (the hierarchy of the file) is applied to the points and the
 * normals, a transform that mirrors reverses the winding of the faces, and the up axis is
 * corrected. The meshes are merged into one unless options.mergeMeshes is off.
 *
 * @param declaredUp The axis the file's format calls up, used when the options say Auto and the
 * file does not declare one itself.
 * @throws ImportError when the file holds no polygon.
 */
ImportedFile buildMeshes(
    RawScene&& scene,
    const ImportOptions& options,
    UpAxis declaredUp,
    const std::string& readerName
);

} // namespace helix::import
