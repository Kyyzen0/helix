#pragma once
#include "Graphics/Metal.h"
#include <string>
#include <vector>

namespace helix::gfx {

/// @brief What the Metal device of a graphics device can do.
struct GraphicsDeviceFeatures
{
    /// @brief The name of the GPU, such as "Apple M4".
    std::string deviceName;

    /// @brief Whether the CPU and the GPU share their memory, which every Apple silicon Mac does.
    ///
    /// The renderer then keeps its geometry in shared buffers that the CPU fills in place,
    /// with no staging copy.
    bool unifiedMemory = false;

    /// @brief Whether the GPU is an Apple GPU, which keeps multisampled targets in tile memory.
    bool appleGpu = false;
};

/// @brief The Metal device the viewport renderers draw with, and the shaders they draw with.
///
/// A host that already has a Metal device, such as the scene graph of Qt Quick, hands it in so
/// that every texture the renderer makes can be used with it. Any number of graphics devices can
/// exist at once, each one holding its own shader libraries.
class GraphicsDevice
{
  public:
    /// @brief The message of the error thrown when the machine has no Metal device.
    ///
    /// The application prints the same message, which the smoke test of the CI looks for.
    static constexpr const char* noDeviceMessage = "No Metal device available";

    /// @brief Returns whether the machine has a Metal device.
    static bool isMetalAvailable();

    /// @brief Creates the device and loads the shaders.
    ///
    /// @param device the Metal device to draw with, or null for the default device of the machine.
    /// @param shaderLibraryPath the metallib to load. When empty, the library is looked for in
    /// `HELIX_METALLIB`, then in the resources of the application bundle, then in the build tree.
    /// When no library is found, a development build compiles the shader sources instead
    /// (see `GraphicsDevice.cpp`).
    ///
    /// @throws std::runtime_error with `noDeviceMessage` when no device exists, or with the
    /// reason when the shaders cannot be loaded.
    explicit GraphicsDevice(MTL::Device* device = nullptr, const std::string& shaderLibraryPath = {});
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /// @brief Returns the Metal device.
    MTL::Device* getDevice() const { return device_.get(); }

    /// @brief Returns the queue frames are submitted on when the renderer draws by itself.
    ///
    /// The queue is made on first use, since a host that records into its own command buffer never
    /// needs one.
    MTL::CommandQueue* getCommandQueue();

    /// @brief Returns the shader function of that name, or null when no library holds it.
    NS::SharedPtr<MTL::Function> getFunction(const char* name) const;

    /// @brief Returns what the device can do.
    const GraphicsDeviceFeatures& getFeatures() const { return features_; }

  private:
    /// @brief Loads the metallib, or compiles the sources of a development build.
    void loadLibraries(const std::string& shaderLibraryPath);

    NS::SharedPtr<MTL::Device> device_;
    NS::SharedPtr<MTL::CommandQueue> commandQueue_;
    std::vector<NS::SharedPtr<MTL::Library>> libraries_;
    GraphicsDeviceFeatures features_;
};

} // namespace helix::gfx
