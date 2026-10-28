#pragma once
#include "Engine/Core/Types.h"
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace helix {
namespace attr {
template <typename T> using StoreContainer = std::vector<T>;

template <typename T> class AttributeHandle;

/**
 * @class helix::attr::Attribute
 * @brief Containers for geometry data.
 *
 * Attributes are the containers for geometry data in the engine.
 * Each attribute is a single column in the spreadsheet that makes up your geometry.
 * The column has a single name and type. Based on the attribute owner it will also
 * have a value for each element. For example, if the owner is a point then your
 * attribute might be Color, your type might be a vector, and the attribute will
 * have a color value for each point in the mesh. Attributes own the underlying
 * storage for their values, which is contiguous and strongly typed
 * (e.g., ints, floats, vectors, bools). This allows a single attribute class
 * to store data regardless of the type without templates.
 * Attributes cannot be read or written from by themselves.
 * For that an attribute handle is required, (see #helix::attr::AttributeHandle).
 *
 * @note Values are strongly typed; invalid type combinations are guarded both
 *       at compile time (via handles) and at runtime (defensive checks).
 */
class Attribute
{
  public:
    /**
     * @brief Construct a new attribute and initialize its typed storage.
     *
     * @param name Human-readable identifier, spaces are not permitted (unique within a
     * collection/scope).
     * @param type Attribute data type that values will be stored in.
     *
     */
    Attribute(
        std::string name,
        attr::AttributeType type,
        bool intrinsic = false,
        bool isInternal = false
    );
    /**
     * @brief Copies the attribute without copying its values.
     *
     * The copy and the original share one store until either of them is written to. The write
     * paths (@ref resize, @ref compact and the read write @ref AttributeHandle) call @ref detach
     * first, so a write never reaches the other holder.
     */
    Attribute(const Attribute& other);
    Attribute& operator=(const Attribute& other);
    /**
     * @brief Gives this attribute a store of its own when it shares one.
     *
     * Does nothing when the store is already exclusive. Safe to call from several threads on the
     * same attribute: the store is cloned once.
     *
     * @note Handles bound before the call keep pointing at the shared store. Rebind them.
     */
    void detach();
    /**
     * @brief Returns whether the store may be shared with another attribute.
     *
     * @note Errs on the side of true: the original of a copy stays flagged until it detaches.
     */
    bool isStoreShared() const { return storeShared_.load(std::memory_order_acquire); }
    /**
     * @brief Returns the attribute type this attribute stores.
     */
    AttributeType getType() const;
    /**
     * @brief Returns the name of this attribute.
     */
    const std::string& getName() const;

    Vector3 getVector3(Offset offset) const;
    Matrix4 getMatrix4(Offset offset) const;
    size_t getSize() const;
    /**
     * @brief Returns the number of components in the type (eg. StringT is 1, vectorT is 3).
     */
    unsigned int getTypeSize() const;

    /**
     * @brief Returns whether the attribute is intrinsic
     *
     * An intrinsic attribute is an inherent property of the geometry created with the geometry
     * automatically and cannot be deleted.
     */
    bool isIntrinsic() const;

    /**
     * @brief Returns whether the engine maintains this attribute.
     *
     * @note Nodes can't write to an internal attribute and it is hidden from the user.
     */
    bool isInternal() const;

    /**
     *  @brief Changes the number of elements stored
     */
    void resize(size_t size);

    /**
     * @brief Removes entries marked as deleted, defragmenting the storage
     * so offsets are contiguous again.
     */
    void compact(const std::vector<bool>& keep);

    template <typename T> friend class AttributeHandle;
    template <typename T> friend class AttributeHandleRO;

  private:
    void markShared() const
    {
        if (!storeShared_.load(std::memory_order_relaxed))
            storeShared_.store(true, std::memory_order_release);
    }

    bool internal_ = false;
    bool intrinsic_ = false;

    attr::AttributeType type_;
    unsigned int typeSize_ = 1;

    std::string name_;

    using StoreVariant = std::variant<
        std::shared_ptr<StoreContainer<intT>>,
        std::shared_ptr<StoreContainer<floatT>>,
        std::shared_ptr<StoreContainer<helix::Vector3>>,
        std::shared_ptr<StoreContainer<helix::boolT>>,
        std::shared_ptr<StoreContainer<helix::Matrix4>>>;
    StoreVariant store_;
    // Set on both sides of a copy. The flag, not use_count(), decides when to clone: a mesh holds
    // several references on its stores through its handles, so use_count() would clone on every
    // write.
    mutable std::atomic<bool> storeShared_{false};
};

using attribVector = std::vector<std::shared_ptr<attr::Attribute>>;
} // namespace attr
} // namespace helix
