#pragma once
#include "Engine/Primitives/Primitive.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace helix {

class NodePacket
{
  public:
    // Lazy forward iterator that flattens transforms across all primitives.
    // See NodePacket.cpp for Iterator method implementations.
    struct Transforms
    {
        struct Iterator
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = Transform;

            Iterator(
                std::vector<geo::PrimPtr>& primitives,
                TransformClass transformClass,
                size_t primIdx
            );

            Transform operator*() const;
            Iterator& operator++();
            Iterator operator++(int);
            friend bool operator==(const Iterator& a, const Iterator& b)
            {
                return a.primIdx_ == b.primIdx_ && a.offset_ == b.offset_;
            }
            friend bool operator!=(const Iterator& a, const Iterator& b) { return !(a == b); }

          private:
            std::vector<geo::PrimPtr>& primitives_;
            TransformClass transformClass_;
            size_t primIdx_;
            Offset offset_ = 0;
            std::shared_ptr<attr::Attribute> curAttrib_;
            size_t curSize_ = 0;
            std::vector<Transform> orientations_;

            void advance();
        };

        Transforms(std::vector<geo::PrimPtr>& primitives, TransformClass transformClass)
            : primitives_(primitives), transformClass_(transformClass)
        {
        }

        Iterator begin() { return Iterator(primitives_, transformClass_, 0); }
        Iterator end() { return Iterator(primitives_, transformClass_, primitives_.size()); }

      private:
        std::vector<geo::PrimPtr>& primitives_;
        TransformClass transformClass_;
    };

    void addPrimitive(std::shared_ptr<helix::geo::Primitive> primitive);
    void attemptMerge(geo::PrimPtr prim);

    std::shared_ptr<helix::geo::Primitive> getPrimitive(unsigned int index);
    std::shared_ptr<const helix::geo::Primitive> getPrimitive(unsigned int index) const;
    geo::PrimPtr getPrimAtPath(const std::string& path);
    std::shared_ptr<const geo::Primitive> getPrimAtPath(const std::string& path) const;

    const std::vector<std::shared_ptr<helix::geo::Primitive>>& getPrimitives() const
    {
        return primitives_;
    }
    std::vector<geo::PrimPtr> getPrimitives(helix::geo::PrimType type) const;
    void removePrim(std::string path);

    Transforms getTransforms(TransformClass transformClass)
    {
        return Transforms(primitives_, transformClass);
    }

    size_t size() const;
    NodePacket deepCopy() const;

    /**
     * @brief Returns a number that tells this packet from every other one a node published.
     *
     * A node stamps the packet it hands to an output (nt::Node::setOutputPacket), drawing from one
     * counter shared by every node, so two different results never carry the same number. A
     * packet nothing stamped, such as the empty one an output holds before a cook, reads zero.
     * The cook cache files a node's output under the numbers of the packets that came in, so
     * the output is only reused when its inputs are the very packets it was made from.
     *
     * @note A copy keeps the number of the packet it was copied from, until it is stamped itself.
     */
    std::uint64_t identity() const { return identity_; }

    /// @brief Gives the packet a number of its own, see identity.
    void stamp();

    /**
     * @brief Estimates how much memory the packet holds, in bytes.
     *
     * Adds up every attribute and group of every primitive, the intrinsic ones included. A store
     * that two attributes share (copy on write, see attr::Attribute) is counted once for each of
     * them, so the estimate errs on the high side. A texture of a material is not counted, since
     * every copy of a mesh shares it.
     */
    std::size_t estimateBytes() const;

  private:
    std::vector<geo::PrimPtr> primitives_;
    std::uint64_t identity_ = 0;
};

} // namespace helix
