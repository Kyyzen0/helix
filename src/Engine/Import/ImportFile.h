#pragma once
#include "Engine/Import/RawScene.h"
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace helix::geo {
class Mesh;
}

namespace helix::import {

/// @brief What the import is asked for, besides the file.
struct ImportOptions
{
    UpAxis upAxis = UpAxis::Auto;
    /// @brief Whether the meshes of the file become one mesh.
    bool mergeMeshes = true;
    /// @brief The Blender to convert a .blend file with. Empty looks for an installed one.
    std::string blenderPath;
};

/// @brief What was read, for a person to see.
struct ImportSummary
{
    std::size_t meshCount = 0;
    std::size_t pointCount = 0;
    std::size_t faceCount = 0;
    bool hasNormals = false;
    bool hasUvs = false;
    bool hasColors = false;
    std::size_t materialCount = 0;
    /// @brief Materials the file gives a base colour texture to, loaded or not.
    std::size_t texturedMaterialCount = 0;
    /// @brief Distinct base colour pictures decoded (a picture several materials share counts once).
    std::size_t decodedTextureCount = 0;
    /// @brief The memory those pictures take, in bytes.
    std::size_t textureBytes = 0;
    /// @brief How many of them were scaled down to the largest texture side.
    std::size_t scaledTextureCount = 0;
    /// @brief One line per texture the file refers to and the import could not load.
    std::vector<std::string> missingTextures;
    /// @brief The base colour of the first material, when there is one.
    bool hasBaseColor = false;
    std::array<float, 4> baseColor{1.f, 1.f, 1.f, 1.f};
    /// @brief Animations the file holds (some formats are read without theirs: see jointCount).
    std::size_t animationCount = 0;
    std::size_t skinnedMeshCount = 0;
    std::size_t skippedPrimitiveCount = 0;
    /// @brief The joints of the skeleton the import built, 0 when the file has none.
    std::size_t jointCount = 0;
    std::size_t rootJointCount = 0;
    /// @brief The clips the import kept, with their length in seconds.
    std::vector<std::pair<std::string, float>> clips;
    /// @brief Vertices that more than four joints pull on, which keep their four strongest.
    std::size_t overInfluencedVertices = 0;
    /// @brief Tracks that are cubic splines in the file, played as straight lines between keys.
    std::size_t cubicTracks = 0;
    /// @brief The reader that was used, such as "Assimp" or "Model I/O".
    std::string reader;

    /// @brief The summary as one or two sentences, such as "2 meshes, 1536 points ...".
    std::string text() const;
};

/// @brief The geometry of a file, with every transform of its hierarchy and the up axis applied.
struct ImportedFile
{
    std::vector<std::shared_ptr<const geo::Mesh>> meshes;
    /// @brief The skeleton of the file (a mesh whose points are the joints), or null when no mesh
    /// of the file is skinned. The skin attributes of the meshes index its points.
    std::shared_ptr<const geo::Mesh> skeleton;
    ImportSummary summary;
};

/// @brief The extensions the import reads, lower case and with the dot, such as ".glb".
std::vector<std::string> supportedExtensions();

/// @brief Returns whether the import reads files with this extension.
bool isSupportedExtension(const std::string& extension);

/// @brief The supported extensions as a sentence, such as ".glb, .gltf and .fbx".
std::string supportedExtensionsText();

/// @brief Names the folder the results of a .blend conversion are kept in.
/// @note Without one the conversions go to HelixBlendCache in the temporary folder.
void setBlendCacheDirectory(const std::filesystem::path& directory);

/// @brief The folder a conversion is kept in.
std::filesystem::path blendCacheDirectory();

/**
 * @brief Reads a file into meshes.
 *
 * The reader is picked by extension: Assimp for glTF, GLB and the formats it reads well, Model
 * I/O for USD, and the engine's own obj reader for obj. A .blend file is converted to GLB by
 * Blender first.
 *
 * The result is kept in memory, and read again only when the path, the size, the date of the file
 * or one of the options changes.
 *
 * @param[out] fromCache Set to whether the file was not read this time.
 * @throws ImportError with a message fit for a person when the file cannot be imported.
 */
std::shared_ptr<const ImportedFile> importFile(
    const std::filesystem::path& file,
    const ImportOptions& options,
    bool* fromCache = nullptr
);

/// @brief Forgets what importFile kept.
void clearImportCache();

/// @brief Forgets what importFile kept of one file, in every setting it was read with. The other
/// files stay in the cache.
/// @return The number of entries that were dropped.
std::size_t forgetImportedFile(const std::filesystem::path& file);

/// @brief The number of times a file was really read (not served from the cache) since the start.
std::size_t importReadCount();

} // namespace helix::import
