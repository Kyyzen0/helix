#pragma once
#include "Graphics/VulkanHandles.h"
#include "Graphics/VulkanLoader.h"
#include <memory>
#include <string>
#include <vector>

namespace enzo::gfx {

/// @brief What the driver of a graphics device can do.
struct GraphicsDeviceFeatures
{
    /// @brief The Vulkan version the device runs, packed as `VK_MAKE_API_VERSION`.
    uint32_t apiVersion = 0;

    /// @brief The name the driver gives itself, which is "MoltenVK" on macOS.
    std::string driverName;

    /// @brief The description of the adapter, which is the name of the Metal device on macOS.
    std::string adapterDescription;

    /// @brief Whether the driver is a portability subset driver.
    ///
    /// A portability subset driver offers a reduced set of features, which is how MoltenVK
    /// reaches the Metal feature set of macOS.
    bool portabilityDriver = false;
};

/// @brief The one Vulkan device in the process, shared by the host and every viewport.
///
/// @note Destroy every host object using the device first, since destroying
/// this destroys the Vulkan device and instance.
class GraphicsDevice
{
  public:
    /// @brief Creates the device.
    ///
    /// @param instanceExtensions the extra instance extensions the host needs.
    /// @throws std::runtime_error when a device already exists or no Vulkan device is found.
    explicit GraphicsDevice(const std::vector<std::string>& instanceExtensions = {});
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /// @brief Returns the device of this process.
    ///
    /// @throws std::runtime_error when no device exists.
    static GraphicsDevice& get();

    /// @brief Returns the handles a host adopts to share the device.
    VulkanHandles getVulkanHandles() const;

    /// @brief Returns what the driver of the device can do.
    const GraphicsDeviceFeatures& getFeatures() const { return features_; }

    /// @brief Returns what the host offers as a Vulkan loader.
    const VulkanLoaderStatus& getLoaderStatus() const { return loaderStatus_; }

    /// @brief Returns whether the device was lost.
    ///
    /// A device is lost when the driver takes it away, which happens on macOS when the
    /// Metal device of the machine goes away with it.
    bool isDeviceLost() const { return deviceLost_; }

    /// @brief Marks the device as lost, so that the host can build another one.
    void markDeviceLost() { deviceLost_ = true; }

    /// @brief Returns the last error the engine reported, empty when it reported none.
    ///
    /// The engine holds back some of its errors rather than throwing them, so this is
    /// where a host reads what went wrong.
    static const std::string& getLastEngineError();

    /// @brief Forgets the last error the engine reported.
    static void clearLastEngineError();

    struct DiligentObjects;

    /// @brief Returns the Diligent device and context the renderer draws with.
    ///
    /// @note The type is complete only inside enzoGraphics, through `Graphics/DiligentObjects.h`.
    DiligentObjects& getDiligentObjects() const;

  private:
    std::unique_ptr<DiligentObjects> diligent_;
    /// @brief What the host offered as a Vulkan loader.
    VulkanLoaderStatus loaderStatus_;
    /// @brief What the driver of the device can do.
    GraphicsDeviceFeatures features_;
    /// @brief Whether the driver took the device away.
    bool deviceLost_ = false;
};

} // namespace enzo::gfx
