#pragma once
#include "Engine/Core/Types.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace helix::import {

/// @brief What goes wrong while a file is read, in words meant for a person.
class ImportError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
    // Defined in ImportFile.cpp, so the type has one home and an exception that crosses from the
    // engine into a node library is caught as the same type on both sides.
    ~ImportError() override;
};

/// @brief Which axis a file calls up.
enum class UpAxis
{
    /// @brief Whatever the file declares, and the convention of its format when it declares nothing.
    Auto,
    Y,
    Z
};

/**
 * @brief One mesh as a reader found it, before the engine has a say.
 *
 * Every array is per vertex of the source mesh (a corner shared by several faces appears once),
 * and the faces index into them. A reader fills only what the file carries: an empty array means
 * the file has none.
 */
struct RawMesh
{
    std::string name;
    /// @brief Three floats per vertex.
    std::vector<float> positions;
    /// @brief Three floats per vertex, or empty.
    std::vector<float> normals;
    /// @brief Two floats per vertex (the first channel), or empty.
    std::vector<float> uvs;
    /// @brief Three floats per vertex (the first colour set, without alpha), or empty.
    std::vector<float> colors;
    /// @brief The number of corners of each face.
    std::vector<std::uint32_t> faceCounts;
    /// @brief The vertex of every corner, face after face.
    std::vector<std::uint32_t> indices;
    /// @brief Index into RawScene::materials, or -1 when the mesh has none.
    int material = -1;
};

/// @brief One placement of a mesh in the scene. A mesh used twice has two instances.
struct RawInstance
{
    std::size_t mesh = 0;
    /// @brief The transform of the whole hierarchy above the mesh, column major.
    Matrix4 world = Matrix4::Identity();
};

struct RawMaterial
{
    std::string name;
    /// @brief The base colour, each channel from 0 to 1.
    std::array<float, 4> baseColor{1.f, 1.f, 1.f, 1.f};
    bool hasTexture = false;
};

/// @brief What a reader hands the engine: meshes, where they sit, and what was left behind.
struct RawScene
{
    std::vector<RawMesh> meshes;
    std::vector<RawInstance> instances;
    std::vector<RawMaterial> materials;
    /// @brief The axis the file declares up, or Auto when its format says nothing.
    UpAxis declaredUp = UpAxis::Auto;
    /// @brief Skins, animations and the like the file holds and the import leaves out.
    std::size_t animationCount = 0;
    std::size_t skinnedMeshCount = 0;
    /// @brief Points and lines the file holds and the import leaves out.
    std::size_t skippedPrimitiveCount = 0;
};

} // namespace helix::import
