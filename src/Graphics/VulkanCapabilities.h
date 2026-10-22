#pragma once
#include <cstdint>
#include <string>

namespace enzo::gfx {

/// @brief The Vulkan version the viewport asks a driver for.
///
/// The viewport draws with the render pass and the pipeline features of Vulkan 1.3,
/// so it never needs more. Asking for the same version on every host keeps the
/// behaviour of a desktop driver and of MoltenVK the same.
inline constexpr uint32_t requiredVulkanVersion = (1u << 22) | (3u << 12);

/// @brief Packs a major and a minor version the way VK_MAKE_API_VERSION does.
inline constexpr uint32_t packVulkanVersion(uint32_t major, uint32_t minor)
{
    return (major << 22) | (minor << 12);
}

/// @brief Returns the major part of a packed Vulkan version.
inline constexpr uint32_t vulkanVersionMajor(uint32_t version) { return version >> 22; }

/// @brief Returns the minor part of a packed Vulkan version.
inline constexpr uint32_t vulkanVersionMinor(uint32_t version) { return (version >> 12) & 0x3FFu; }

/// @brief Returns the version to ask the driver for.
///
/// @param loaderVersion the highest version the loader offers, packed as VK_MAKE_API_VERSION,
/// or zero when the loader announces nothing.
///
/// The loader never offers more than it has and the viewport needs no more than
/// `requiredVulkanVersion`, so the answer is the smaller of the two. A loader that
/// announces nothing is taken at its oldest, which the extensions carry.
inline constexpr uint32_t chooseVulkanVersion(uint32_t loaderVersion)
{
    if (loaderVersion == 0) return packVulkanVersion(1, 0);
    if (loaderVersion < requiredVulkanVersion) return loaderVersion;
    return requiredVulkanVersion;
}

/// @brief Returns a readable form of a packed Vulkan version, such as "1.3".
inline std::string describeVulkanVersion(uint32_t version)
{
    return std::to_string(vulkanVersionMajor(version)) + "." + std::to_string(vulkanVersionMinor(version));
}

/// @brief Returns whether a driver name belongs to a portability subset driver.
///
/// A portability subset driver offers a reduced set of features, which is how
/// MoltenVK reaches the Metal feature set of macOS.
inline bool isPortabilityDriver(const std::string& driverName)
{
    return driverName.find("MoltenVK") != std::string::npos ||
           driverName.find("Portability") != std::string::npos;
}

/// @brief Returns whether a driver name belongs to MoltenVK.
inline bool isMoltenVkDriver(const std::string& driverName)
{
    return driverName.find("MoltenVK") != std::string::npos;
}

} // namespace enzo::gfx
