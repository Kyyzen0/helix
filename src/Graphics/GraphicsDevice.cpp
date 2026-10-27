// How the shaders are found.
//
// The shaders are compiled into `helix.metallib` at build time (see `CMakeLists.txt`), and the
// library is looked for, in this order, at:
//
// 1. the path the caller passes, which is the only place looked at when it is given;
// 2. the path in the environment variable `HELIX_METALLIB`;
// 3. `../Resources/helix.metallib` beside the executable, which is the Resources folder of
//    the application bundle, and of the build tree, whose tests sit in `Contents/bin`;
// 4. the path of the build tree, which is baked in as `HELIX_DEV_METALLIB`.
//
// A machine that lacks the offline Metal compiler cannot build the library. A development build
// there still runs: when no library is found, the `.metal` sources in `HELIX_METAL_SHADER_DIR`
// (or in the source tree, baked in as `HELIX_DEV_SHADER_DIR`) are compiled by the Metal runtime
// instead. That costs some startup time and is never the way a packaged build runs.

#include "Graphics/GraphicsDevice.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mach-o/dyld.h>
#include <sstream>
#include <stdexcept>

namespace helix::gfx {

namespace {

namespace fs = std::filesystem;

constexpr const char* metallibName = "helix.metallib";

/// @brief Returns the text of NS::Error, or "unknown error" when there is none.
std::string describeError(const NS::Error* error)
{
    if (!error || !error->localizedDescription()) return "unknown error";
    return error->localizedDescription()->utf8String();
}

/// @brief Returns the folder of the running executable, empty when it is unknown.
fs::path getExecutableDir()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));

    std::error_code ignored;
    const fs::path executable = fs::weakly_canonical(buffer, ignored);
    return executable.parent_path();
}

/// @brief Returns the places the metallib is looked for, in order.
std::vector<fs::path> getMetallibCandidates()
{
    std::vector<fs::path> candidates;
    if (const char* fromEnvironment = std::getenv("HELIX_METALLIB"); fromEnvironment && *fromEnvironment)
        candidates.emplace_back(fromEnvironment);

    const fs::path executableDir = getExecutableDir();
    if (!executableDir.empty()) candidates.push_back(executableDir / ".." / "Resources" / metallibName);

#ifdef HELIX_DEV_METALLIB
    candidates.emplace_back(HELIX_DEV_METALLIB);
#endif
    return candidates;
}

/// @brief Returns the folder of the shader sources of a development build, empty when none is known.
fs::path getShaderSourceDir()
{
    if (const char* fromEnvironment = std::getenv("HELIX_METAL_SHADER_DIR"); fromEnvironment && *fromEnvironment)
        return fromEnvironment;
#ifdef HELIX_DEV_SHADER_DIR
    return HELIX_DEV_SHADER_DIR;
#else
    return {};
#endif
}

/// @brief Returns the whole text of the file.
std::string readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read the shader file " + path.string());
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

/// @brief Compiles every `.metal` file of the folder with the Metal runtime compiler.
///
/// The runtime compiler has no include path, so the include of `ShaderTypes.h` is replaced by
/// the text of the file.
std::vector<NS::SharedPtr<MTL::Library>> compileSources(MTL::Device* device, const fs::path& dir)
{
    const std::string includeLine = "#include \"ShaderTypes.h\"";
    const std::string sharedTypes = readFile(dir / "ShaderTypes.h");

    std::vector<fs::path> sources;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".metal") sources.push_back(entry.path());
    }
    std::ranges::sort(sources);

    std::vector<NS::SharedPtr<MTL::Library>> libraries;
    for (const fs::path& path : sources)
    {
        std::string source = readFile(path);
        if (const std::size_t at = source.find(includeLine); at != std::string::npos)
            source.replace(at, includeLine.size(), sharedTypes);

        NS::SharedPtr<MTL::CompileOptions> options = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
        options->setLanguageVersion(MTL::LanguageVersion3_0);

        NS::Error* error = nullptr;
        NS::SharedPtr<MTL::Library> library =
            NS::TransferPtr(device->newLibrary(toNsString(source.c_str()), options.get(), &error));
        if (!library)
        {
            throw std::runtime_error(
                "Could not compile the shader " + path.filename().string() + ": " + describeError(error)
            );
        }
        libraries.push_back(library);
    }
    return libraries;
}

} // namespace

bool GraphicsDevice::isMetalAvailable()
{
    ScopedAutoreleasePool pool;
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device) return false;
    device->release();
    return true;
}

GraphicsDevice::GraphicsDevice(MTL::Device* device, const std::string& shaderLibraryPath)
{
    ScopedAutoreleasePool pool;
    device_ = device ? NS::RetainPtr(device) : NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!device_) throw std::runtime_error(noDeviceMessage);

    features_.deviceName = device_->name() ? device_->name()->utf8String() : "";
    features_.unifiedMemory = device_->hasUnifiedMemory();
    features_.appleGpu = device_->supportsFamily(MTL::GPUFamilyApple1);

    loadLibraries(shaderLibraryPath);
}

GraphicsDevice::~GraphicsDevice() = default;

MTL::CommandQueue* GraphicsDevice::getCommandQueue()
{
    if (!commandQueue_)
    {
        commandQueue_ = NS::TransferPtr(device_->newCommandQueue());
        if (!commandQueue_) throw std::runtime_error("The Metal device could not make a command queue");
    }
    return commandQueue_.get();
}

NS::SharedPtr<MTL::Function> GraphicsDevice::getFunction(const char* name) const
{
    ScopedAutoreleasePool pool;
    for (const NS::SharedPtr<MTL::Library>& library : libraries_)
    {
        // newFunction returns an object the caller owns, or null when the library lacks the name.
        if (MTL::Function* function = library->newFunction(toNsString(name)))
            return NS::TransferPtr(function);
    }
    return {};
}

void GraphicsDevice::loadLibraries(const std::string& shaderLibraryPath)
{
    // A path the caller names is the only place looked at, so a typo is an error rather than
    // a silent fall back on another library.
    std::vector<fs::path> candidates;
    if (!shaderLibraryPath.empty())
        candidates.emplace_back(shaderLibraryPath);
    else
        candidates = getMetallibCandidates();

    std::string tried;
    for (const fs::path& candidate : candidates)
    {
        std::error_code ignored;
        if (!fs::is_regular_file(candidate, ignored))
        {
            tried += "\n  " + candidate.lexically_normal().string();
            continue;
        }

        NS::Error* error = nullptr;
        NS::SharedPtr<MTL::Library> library =
            NS::TransferPtr(device_->newLibrary(toNsString(candidate.c_str()), &error));
        if (!library)
        {
            throw std::runtime_error(
                "Could not load the shaders from " + candidate.string() + ": " + describeError(error)
            );
        }
        libraries_.push_back(library);
        return;
    }

    // A development build without a compiled library compiles the sources instead.
    const fs::path sourceDir = shaderLibraryPath.empty() ? getShaderSourceDir() : fs::path();
    std::error_code ignored;
    if (!sourceDir.empty() && fs::is_directory(sourceDir, ignored))
    {
        libraries_ = compileSources(device_.get(), sourceDir);
        return;
    }

    throw std::runtime_error(
        std::string("The shader library ") + metallibName + " was not found. Looked at:" + tried
    );
}

} // namespace helix::gfx
