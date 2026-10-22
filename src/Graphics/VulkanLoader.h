#pragma once
#include "Graphics/VulkanCapabilities.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#define ENZO_VULKAN_LOADER_WINDOWS 1
#include <windows.h>
#else
#define ENZO_VULKAN_LOADER_POSIX 1
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace enzo::gfx {

/// @brief What the host offers as a Vulkan loader.
struct VulkanLoaderStatus
{
    /// @brief Whether a loader was opened.
    bool loaded = false;
    /// @brief The path of the loader that was opened, empty when none was found.
    std::string path;
    /// @brief The handle of the opened loader, null when none was found.
    ///
    /// The handle reaches the entry points of the loader, such as vkGetInstanceProcAddr.
    void* library = nullptr;
    /// @brief Whether the loader was opened by its name, which is how the engine opens it.
    ///
    /// A loader that is only reachable by its path is of no use to the engine, which opens
    /// it by its name and finds it through the run path of the program.
    bool byName = false;
    /// @brief The highest Vulkan version the loader offers, packed as VK_MAKE_API_VERSION.
    uint32_t version = 0;
    /// @brief The path of the manifest that names the driver, empty when the host names one.
    std::string driverManifest;
    /// @brief The paths that were tried, in the order they were tried.
    std::vector<std::string> tried;
    /// @brief Why the last attempt failed, empty when a loader was opened.
    std::string error;
};

namespace detail {

/// @brief Opens a library and returns its handle, or null when it cannot be opened.
inline void* openLibrary(const std::string& path, bool alreadyLoaded)
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    (void)alreadyLoaded;
    return LoadLibraryA(path.c_str());
#else
    int flags = RTLD_NOW | RTLD_LOCAL;
    // A library that is already in the process is taken as it is, without loading it twice.
    if (alreadyLoaded) flags |= RTLD_NOLOAD;
    return dlopen(path.c_str(), flags);
#endif
}

/// @brief Returns a symbol of an open library, or null when it holds no such symbol.
inline void* findSymbol(void* library, const char* name)
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

/// @brief Returns the reason the last attempt to open a library failed.
inline std::string lastLibraryError()
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    const DWORD code = GetLastError();
    return "error " + std::to_string(static_cast<unsigned long>(code));
#else
    const char* error = dlerror();
    return error ? std::string(error) : std::string("the library holds no such symbol");
#endif
}

/// @brief Returns the folder a path lies in, with the separator kept.
inline std::string folderOf(const std::string& path)
{
    const std::size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos) return std::string();
    return path.substr(0, separator + 1);
}

/// @brief Returns the folder the running program lies in, with the separator kept.
inline std::string programFolder()
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0) return std::string();
    return folderOf(std::string(path, length));
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&programFolder), &info) == 0 || !info.dli_fname)
    {
        return std::string();
    }
    return folderOf(std::string(info.dli_fname));
#endif
}

/// @brief Returns the leaf names the engine opens the Vulkan loader by.
inline std::vector<std::string> vulkanLoaderNames()
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    return {"vulkan-1.dll"};
#elif defined(__APPLE__)
    return {"libvulkan.dylib", "libvulkan.1.dylib"};
#else
    return {"libvulkan.so.1", "libvulkan.so"};
#endif
}

/// @brief Returns the version the loader offers.
inline uint32_t loaderVersion(void* library)
{
    using EnumerateVersion = uint32_t (*)(uint32_t*);
    const auto enumerateVersion = reinterpret_cast<EnumerateVersion>(findSymbol(library, "vkEnumerateInstanceVersion"));
    if (!enumerateVersion) return packVulkanVersion(1, 0);

    uint32_t version = 0;
    if (enumerateVersion(&version) != 0) return packVulkanVersion(1, 0);
    return version;
}

/// @brief Returns whether a file can be read.
inline bool fileExists(const std::string& path)
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path.c_str(), R_OK) == 0;
#endif
}

/// @brief Names an environment variable for the process.
inline void setEnvironment(const char* name, const std::string& value)
{
#if defined(ENZO_VULKAN_LOADER_WINDOWS)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

} // namespace detail

/// @brief Returns the paths the Vulkan loader is looked for in, in the order they are tried.
///
/// The engine opens the loader by its leaf name, which the dynamic loader only finds in the
/// system folders. macOS has no Vulkan driver of its own, so the loader and the MoltenVK
/// driver come from the Vulkan SDK and the MoltenVK package, which a build tree keeps in
/// its own folders rather than in the system ones.
inline std::vector<std::string> vulkanLoaderCandidates()
{
    std::vector<std::string> candidates;

    // A path the user names wins over every other place.
    if (const char* named = std::getenv("ENZO_VULKAN_LOADER")) candidates.push_back(named);

#if defined(__APPLE__)
    // Where the Vulkan SDK installs the loader, which the engine also falls back on.
    candidates.push_back("/usr/local/lib/libvulkan.dylib");
    candidates.push_back("/usr/local/lib/libvulkan.1.dylib");
#endif

    // Beside the running program, where a bundle carries the libraries it needs.
    const std::string programFolder = detail::programFolder();
    if (!programFolder.empty())
    {
        for (const std::string& name : detail::vulkanLoaderNames())
        {
            candidates.push_back(programFolder + name);
        }
    }

    // Beside the driver, which a build tree keeps next to the loader.
    if (const char* driver = std::getenv("VK_ICD_FILENAMES"))
    {
        const std::string driverFolder = detail::folderOf(driver);
        if (!driverFolder.empty())
        {
            for (const std::string& name : detail::vulkanLoaderNames())
            {
                candidates.push_back(driverFolder + name);
            }
        }
    }

    return candidates;
}

/// @brief Returns the paths the manifest that names the driver is looked for in.
///
/// The loader reads the variable that names the driver when it creates the instance, so a
/// program that sets it before the instance is made reaches the driver even when it was
/// started without the environment of a shell.
inline std::vector<std::string> vulkanDriverManifestCandidates()
{
    std::vector<std::string> candidates;

    // A manifest the user names wins over every other place.
    if (const char* named = std::getenv("ENZO_VULKAN_ICD")) candidates.push_back(named);

    // Where the Vulkan SDK and the MoltenVK installer put the manifest of the driver.
    candidates.push_back("/usr/local/share/vulkan/icd.d/MoltenVK_icd.json");
    candidates.push_back("/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json");
    candidates.push_back("/etc/vulkan/icd.d/MoltenVK_icd.json");

    // Where a user installs the driver, which needs no rights of an administrator.
    if (const char* home = std::getenv("HOME"))
    {
        candidates.push_back(std::string(home) + "/.local/share/vulkan/icd.d/MoltenVK_icd.json");
    }

    // Beside the program, where a bundle carries the driver it needs.
    const std::string programFolder = detail::programFolder();
    if (!programFolder.empty())
    {
        candidates.push_back(programFolder + "vulkan/icd.d/MoltenVK_icd.json");
        candidates.push_back(programFolder + "../Resources/vulkan/icd.d/MoltenVK_icd.json");
    }

    return candidates;
}

/// @brief Points the loader at the driver, when the host names none.
///
/// @return the path of the manifest that names the driver, empty when there is none.
inline std::string nameVulkanDriver()
{
    // A host that names a driver of its own is left alone.
    if (std::getenv("VK_ICD_FILENAMES") || std::getenv("VK_DRIVER_FILES")) return {};

    for (const std::string& path : vulkanDriverManifestCandidates())
    {
        if (!detail::fileExists(path)) continue;
        detail::setEnvironment("VK_ICD_FILENAMES", path);
        detail::setEnvironment("VK_DRIVER_FILES", path);
        return path;
    }
    return {};
}

/// @brief Makes the Vulkan loader reachable by the name the engine opens it with.
///
/// The engine opens the loader by its leaf name, which the dynamic loader finds through
/// the run path of the program, through DYLD_LIBRARY_PATH or in the system folders. The
/// loader is opened here the same way first, so that the version it offers and the name
/// of the driver can be read, and so that a host that keeps the loader elsewhere says so.
///
/// @return what the host offers as a loader, and which paths were tried.
inline VulkanLoaderStatus prepareVulkanLoader()
{
    VulkanLoaderStatus status;

    // Names the driver of the host, so that the loader finds it even when the program was
    // started without the environment of a shell.
    status.driverManifest = nameVulkanDriver();

    // A loader that is already in the process is taken as it is.
    for (const std::string& name : detail::vulkanLoaderNames())
    {
        status.tried.push_back(name);
        if (void* library = detail::openLibrary(name, true))
        {
            status.loaded = true;
            status.byName = true;
            status.path = name;
            status.library = library;
            status.version = detail::loaderVersion(library);
            return status;
        }
    }

    // The way the engine opens the loader, which finds it through the run path of the
    // program, through DYLD_LIBRARY_PATH or in the system folders.
    for (const std::string& name : detail::vulkanLoaderNames())
    {
        if (void* library = detail::openLibrary(name, false))
        {
            status.loaded = true;
            status.byName = true;
            status.path = name;
            status.library = library;
            status.version = detail::loaderVersion(library);
            return status;
        }
    }

    // A loader that lies elsewhere, which the engine cannot open by its name.
    for (const std::string& path : vulkanLoaderCandidates())
    {
        status.tried.push_back(path);
        if (void* library = detail::openLibrary(path, false))
        {
            status.loaded = true;
            status.path = path;
            status.library = library;
            status.version = detail::loaderVersion(library);
            return status;
        }
        status.error = detail::lastLibraryError();
    }

    return status;
}

/// @brief Returns the message that tells what is missing when the host has no Vulkan device.
///
/// macOS has no Vulkan driver of its own, so the device comes from MoltenVK, which translates
/// Vulkan onto Metal. The message names it and says what to do about it.
inline std::string describeVulkanFailure(const VulkanLoaderStatus& loader, const std::string& reason)
{
    std::string message = "No Vulkan device could be created";
    if (!reason.empty()) message += ": " + reason;
    message += ".\n";

    if (!loader.loaded)
    {
        message += "The Vulkan loader could not be opened. The engine opens it by its name, "
                   "which the dynamic loader finds through the run path of the program, through "
                   "DYLD_LIBRARY_PATH or in the system folders.\n";
    }
    else if (!loader.byName)
    {
        message += "The Vulkan loader was found at " + loader.path +
                   ", but the engine opens it by its name, which the dynamic loader finds through "
                   "the run path of the program, through DYLD_LIBRARY_PATH or in the system "
                   "folders. Put the loader on the run path of the program or in /usr/local/lib.\n";
    }

#if defined(__APPLE__)
    message += "macOS has no Vulkan driver of its own. The device comes from MoltenVK, which "
               "translates Vulkan onto Metal, so it needs:\n"
               "  - the Vulkan loader, as libvulkan.dylib on the run path of the program or in "
               "/usr/local/lib,\n"
               "  - VK_ICD_FILENAMES pointing at the MoltenVK manifest, MoltenVK_icd.json,\n"
               "  - a Metal device, which MoltenVK translates onto.";
#else
    message += "The device comes from the Vulkan driver of this machine, so it needs a driver "
               "that offers a Vulkan device.";
#endif

    if (!loader.tried.empty())
    {
        message += "\nThe loader was looked for in:\n";
        for (const std::string& path : loader.tried) message += "  " + path + "\n";
    }
    if (!loader.error.empty()) message += "The last attempt failed with: " + loader.error + "\n";

    return message;
}

} // namespace enzo::gfx
