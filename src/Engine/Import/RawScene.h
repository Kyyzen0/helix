#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Primitives/Material.h"
#include "Engine/Primitives/Skeleton.h"
#include <array>
#include <cstdint>
#include <memory>
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
    /// @brief Four joints per vertex (an index into RawSkeleton::joints) and their four weights,
    /// or empty when the mesh is not skinned. The weights of a vertex sum to 1, and an unused
    /// influence is joint 0 with weight 0.
    std::vector<std::uint32_t> skinJoints;
    std::vector<float> skinWeights;
};

/// @brief One placement of a mesh in the scene. A mesh used twice has two instances.
struct RawInstance
{
    std::size_t mesh = 0;
    /// @brief The transform of the whole hierarchy above the mesh, column major.
    Matrix4 world = Matrix4::Identity();
};

/// @brief One joint of a skeleton, in the space of the file.
struct RawJoint
{
    std::string name;
    /// @brief Index of the parent joint in RawSkeleton::joints, or -1 for a root. A parent always
    /// comes before its children.
    int parent = -1;
    /// @brief The transform of the joint relative to its parent at rest.
    Matrix4 restLocal = Matrix4::Identity();
    /// @brief For a root, the transform of the nodes above it that are not joints. Identity for
    /// a joint that has a parent joint.
    Matrix4 parentBase = Matrix4::Identity();
    /// @brief The transform of the joint at rest in the space of the file, parentBase included.
    Matrix4 restWorld = Matrix4::Identity();
    /// @brief What the file says takes the skinned mesh to the space of the joint: the inverse
    /// bind matrix of a glTF, the offset matrix of Assimp's bones.
    Matrix4 inverseBind = Matrix4::Identity();
};

/// @brief The joints of a file and the clips that animate them.
struct RawSkeleton
{
    std::vector<RawJoint> joints;
    std::vector<std::shared_ptr<const geo::AnimationClip>> clips;
    /// @brief Vertices that more than four joints pull on, which keep their four strongest.
    std::size_t overInfluencedVertices = 0;
    /// @brief Tracks that are cubic splines in the file, played as straight lines between keys.
    std::size_t cubicTracks = 0;
    /// @brief Tracks of the file that name no joint and are left out.
    std::size_t unmatchedTracks = 0;
};

struct RawMaterial
{
    std::string name;
    /// @brief The base colour, each channel from 0 to 1.
    std::array<float, 4> baseColor{1.f, 1.f, 1.f, 1.f};
    /// @brief The file refers to a base colour texture, loaded or not.
    bool hasTexture = false;
    /// @brief The decoded base colour texture, shared by every material of the file that uses
    /// the same picture. Null when there is none or it could not be loaded.
    std::shared_ptr<const geo::Image> texture;
    /// @brief What went wrong with the texture the file refers to, empty when nothing did.
    std::string missingTexture;
};

/// @brief What a reader hands the engine: meshes, where they sit, and what was left behind.
struct RawScene
{
    std::vector<RawMesh> meshes;
    std::vector<RawInstance> instances;
    std::vector<RawMaterial> materials;
    /// @brief The joints and clips of the file. Empty when no mesh is skinned.
    RawSkeleton skeleton;
    /// @brief The axis the file declares up, or Auto when its format says nothing.
    UpAxis declaredUp = UpAxis::Auto;
    /// @brief The animations the file holds and the meshes it skins, counted before the import
    /// keeps them (the clips and joints of `skeleton` are what it kept).
    std::size_t animationCount = 0;
    std::size_t skinnedMeshCount = 0;
    /// @brief Points and lines the file holds and the import leaves out.
    std::size_t skippedPrimitiveCount = 0;
};

} // namespace helix::import
