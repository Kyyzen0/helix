#pragma once
#include "Engine/Import/RawScene.h"
#include <assimp/scene.h>
#include <string>
#include <unordered_map>

namespace helix::import {

/// @brief An Assimp matrix (row major, the translation in the last column) as an Eigen one.
inline Matrix4 assimpMatrix(const aiMatrix4x4& m)
{
    Matrix4 matrix;
    matrix << m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4;
    return matrix;
}

/**
 * @brief Reads the skin and the animations of an Assimp scene.
 *
 * Assimp gives every mesh its own list of bones, each naming a node of the hierarchy, so the
 * joints are collected once for the whole file, by node, and every mesh weights the same joint
 * list. The joints are put in depth first order, so a parent always comes before its children.
 */
class SkeletonReader
{
  public:
    /// @brief Collects the joints of every bone of every mesh of the scene, and the nodes needed
    /// to connect them.
    explicit SkeletonReader(const aiScene& scene);

    bool hasSkeleton() const { return !skeleton_.joints.empty(); }

    /// @brief Fills the skin of a mesh from its bones, keeping the four strongest influences of
    /// every vertex. A mesh with no bone is left unskinned.
    void readSkin(const aiMesh& source, RawMesh& mesh);

    /// @brief Reads the animations of the scene as clips of this skeleton.
    void readClips();

    RawSkeleton take() { return std::move(skeleton_); }

  private:
    const aiScene& scene_;
    RawSkeleton skeleton_;
    std::unordered_map<std::string, std::size_t> jointOfName_;
};

} // namespace helix::import
