#include "Engine/Import/ImportFile.h"
#include "Engine/GeometryAlgorithms/ObjReader.h"
#include "Engine/Import/BlenderConverter.h"
#include "Engine/Import/MeshBuilder.h"
#include "Engine/Import/Readers.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Primitives/Mesh.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <exception>
#include <new>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace helix::import {

namespace fs = std::filesystem;

namespace {

// Which reader a format goes to, and which axis it calls up when the file does not say.
enum class Reader
{
    Assimp,
    ModelIO,
    Obj,
    Blender
};

struct Format
{
    const char* extension;
    Reader reader;
    UpAxis conventionalUp;
};

// CAD and printing formats are Z up by convention, the rest are Y up.
constexpr Format kFormats[] = {
    {".glb", Reader::Assimp, UpAxis::Y},   {".gltf", Reader::Assimp, UpAxis::Y},
    {".fbx", Reader::Assimp, UpAxis::Y},   {".obj", Reader::Obj, UpAxis::Y},
    {".stl", Reader::Assimp, UpAxis::Z},   {".ply", Reader::Assimp, UpAxis::Y},
    {".dae", Reader::Assimp, UpAxis::Y},   {".3ds", Reader::Assimp, UpAxis::Y},
    {".3mf", Reader::Assimp, UpAxis::Z},   {".dxf", Reader::Assimp, UpAxis::Z},
    {".x3d", Reader::Assimp, UpAxis::Y},   {".x", Reader::Assimp, UpAxis::Y},
    {".usd", Reader::ModelIO, UpAxis::Y},  {".usda", Reader::ModelIO, UpAxis::Y},
    {".usdc", Reader::ModelIO, UpAxis::Y}, {".usdz", Reader::ModelIO, UpAxis::Y},
    {".blend", Reader::Blender, UpAxis::Y},
};

// A file bigger than this is refused, rather than read until the machine runs out of memory.
constexpr std::uintmax_t kMaxFileBytes = 2ull * 1024 * 1024 * 1024;

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

const Format* findFormat(const std::string& extension)
{
    const std::string wanted = lower(extension);
    for (const Format& format : kFormats)
        if (wanted == format.extension) return &format;
    return nullptr;
}

std::mutex cacheMutex;
fs::path blendCacheFolder;
std::atomic<std::size_t> readCount{0};

// A file a read depended on besides the main one, as it was when it was read.
struct Dependency
{
    fs::path path;
    std::uintmax_t size = 0;
    long long modified = 0;
};

struct CacheEntry
{
    std::string key;
    std::shared_ptr<const ImportedFile> file;
    std::vector<Dependency> dependencies;
};

// Whether a dependency is still the file that was read: present, with the same size and date.
bool unchanged(const Dependency& dependency)
{
    std::error_code error;
    const std::uintmax_t size = fs::file_size(dependency.path, error);
    if (error || size != dependency.size) return false;
    const auto modified = fs::last_write_time(dependency.path, error).time_since_epoch().count();
    return !error && static_cast<long long>(modified) == dependency.modified;
}
// The last few files, the newest first. Large scenes are big, so only a couple are kept.
std::deque<CacheEntry> cache;
constexpr std::size_t kCacheCapacity = 2;

std::string upAxisName(UpAxis axis)
{
    switch (axis)
    {
    case UpAxis::Auto:
        return "auto";
    case UpAxis::Y:
        return "y";
    case UpAxis::Z:
        return "z";
    }
    return "auto";
}

// Reads an obj file with the engine's reader, which keeps its points and faces exactly as the
// node has always produced them.
ImportedFile readObjFile(const fs::path& file, const ImportOptions& options)
{
    std::ifstream stream(file);
    if (!stream.is_open()) throw ImportError("Couldn't open " + file.string() + ".");

    auto mesh = std::make_shared<geo::Mesh>();
    try
    {
        utils::readObjInto(stream, *mesh, true);
    }
    catch (...)
    {
        throw ImportError("The obj file is damaged: a line does not hold the number it should.");
    }
    if (mesh->getNumPoints() == 0) throw ImportError("The obj file holds no vertex.");

    // An obj file declares no axis: it is Y up unless the person says otherwise.
    if (options.upAxis == UpAxis::Z)
    {
        Matrix4 zUpToYUp = Matrix4::Identity();
        zUpToYUp(1, 1) = 0.f;
        zUpToYUp(1, 2) = 1.f;
        zUpToYUp(2, 1) = -1.f;
        zUpToYUp(2, 2) = 0.f;
        mesh->applyTransform(zUpToYUp, TransformClass::POINT);
    }

    ImportedFile result;
    result.summary.reader = "obj";
    result.summary.meshCount = 1;
    result.summary.pointCount = mesh->getNumPoints();
    result.summary.faceCount = mesh->getNumFaces();
    for (const auto& attribute : mesh->getAttributes(attr::AttributeOwner::VERTEX))
        if (attribute && attribute->getName() == attr::names::uv) result.summary.hasUvs = true;
    result.meshes.push_back(mesh);
    return result;
}

// @param[out] opened The other files the read used, which the cache watches.
ImportedFile readFile(
    const fs::path& file,
    const Format& format,
    const ImportOptions& options,
    std::vector<fs::path>& opened
)
{
    ++readCount;
    switch (format.reader)
    {
    case Reader::Obj:
        return readObjFile(file, options);
    case Reader::ModelIO:
        return buildMeshes(readWithModelIO(file), options, format.conventionalUp, "Model I/O");
    case Reader::Assimp:
        return buildMeshes(readWithAssimp(file, &opened), options, format.conventionalUp, "Assimp");
    case Reader::Blender:
    {
        const fs::path converted = convertBlendToGlb(file, options.blenderPath);
        return buildMeshes(readWithAssimp(converted), options, UpAxis::Y, "Blender + Assimp");
    }
    }
    throw ImportError("Unsupported format.");
}

} // namespace

ImportError::~ImportError() = default;

std::string ImportSummary::text() const
{
    auto count = [](std::size_t n, const char* one, const char* many) {
        return std::to_string(n) + (n == 1 ? one : many);
    };
    std::ostringstream out;
    out << count(meshCount, " mesh, ", " meshes, ") << count(pointCount, " point, ", " points, ")
        << count(faceCount, " face", " faces");
    std::vector<std::string> carried;
    if (hasNormals) carried.push_back("normals");
    if (hasUvs) carried.push_back("UVs");
    if (hasColors) carried.push_back("vertex colors");
    if (!carried.empty())
    {
        out << " (";
        for (std::size_t i = 0; i < carried.size(); ++i)
            out << (i ? ", " : "") << carried[i];
        out << ")";
    }
    out << ".";
    if (materialCount > 0)
    {
        out << " " << materialCount << (materialCount == 1 ? " material" : " materials");
        if (texturedMaterialCount > 0)
        {
            out << " (" << texturedMaterialCount << " with textures: " << decodedTextureCount << " decoded, "
                << std::fixed << std::setprecision(1) << double(textureBytes) / (1024.0 * 1024.0) << " MB";
            if (scaledTextureCount > 0) out << ", " << scaledTextureCount << " scaled down";
            if (!missingTextures.empty()) out << ", " << missingTextures.size() << " missing";
            out << ")";
        }
        if (hasBaseColor)
            out << ", first base color " << std::fixed << std::setprecision(2) << baseColor[0] << " "
                << baseColor[1] << " " << baseColor[2];
        out << ".";
    }

    if (!missingTextures.empty())
    {
        out << " Missing: " << missingTextures.front();
        if (missingTextures.size() > 1) out << " (and " << missingTextures.size() - 1 << " more)";
        out << ".";
    }

    std::vector<std::string> left;
    if (animationCount > 0)
        left.push_back(std::to_string(animationCount) + (animationCount == 1 ? " animation" : " animations"));
    if (skinnedMeshCount > 0)
        left.push_back(
            std::to_string(skinnedMeshCount) + (skinnedMeshCount == 1 ? " skinned mesh (bind pose only)" : " skinned meshes (bind pose only)")
        );
    if (skippedPrimitiveCount > 0)
        left.push_back(std::to_string(skippedPrimitiveCount) + " points or lines");
    if (!left.empty())
    {
        out << " Left out: ";
        for (std::size_t i = 0; i < left.size(); ++i)
            out << (i ? ", " : "") << left[i];
        out << ".";
    }
    return out.str();
}

std::vector<std::string> supportedExtensions()
{
    std::vector<std::string> extensions;
    for (const Format& format : kFormats)
        extensions.push_back(format.extension);
    return extensions;
}

bool isSupportedExtension(const std::string& extension) { return findFormat(extension) != nullptr; }

std::string supportedExtensionsText()
{
    const std::vector<std::string> extensions = supportedExtensions();
    std::string text;
    for (std::size_t i = 0; i < extensions.size(); ++i)
    {
        if (i > 0) text += i + 1 == extensions.size() ? " and " : ", ";
        text += extensions[i];
    }
    return text;
}

void setBlendCacheDirectory(const fs::path& directory)
{
    const std::lock_guard<std::mutex> lock(cacheMutex);
    blendCacheFolder = directory;
}

fs::path blendCacheDirectory()
{
    const std::lock_guard<std::mutex> lock(cacheMutex);
    if (!blendCacheFolder.empty()) return blendCacheFolder;
    return fs::temp_directory_path() / "HelixBlendCache";
}

void clearImportCache()
{
    const std::lock_guard<std::mutex> lock(cacheMutex);
    cache.clear();
}

std::size_t importReadCount() { return readCount.load(); }

std::shared_ptr<const ImportedFile> importFile(
    const fs::path& file,
    const ImportOptions& options,
    bool* fromCache
)
{
    if (fromCache) *fromCache = false;

    const Format* format = findFormat(file.extension().string());
    if (!format)
    {
        const std::string extension = file.extension().string();
        throw ImportError(
            (extension.empty() ? std::string("A file with no extension") : "Files of type " + extension) +
            " cannot be imported. Helix reads " + supportedExtensionsText() + "."
        );
    }

    std::error_code error;
    if (fs::is_directory(file, error)) throw ImportError(file.string() + " is a folder, not a file.");
    if (!fs::is_regular_file(file, error)) throw ImportError("Couldn't open " + file.string() + ": there is no such file.");
    const std::uintmax_t size = fs::file_size(file, error);
    const auto modified = fs::last_write_time(file, error).time_since_epoch().count();
    if (error) throw ImportError("Couldn't open " + file.string() + ".");
    if (size > kMaxFileBytes)
        throw ImportError(file.string() + " is larger than 2 GB, which Helix does not import.");
    if (size == 0) throw ImportError(file.string() + " is empty.");

    const std::string key = fs::absolute(file, error).string() + "|" + std::to_string(size) + "|" +
                            std::to_string(static_cast<long long>(modified)) + "|" + (options.mergeMeshes ? "merge" : "split") + "|" +
                            upAxisName(options.upAxis) + "|" + options.blenderPath;
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto entry = cache.begin(); entry != cache.end(); ++entry)
        {
            if (entry->key != key) continue;
            // A file the main one points to (a glTF's .bin) can change with the main file as it was.
            if (std::all_of(entry->dependencies.begin(), entry->dependencies.end(), unchanged))
            {
                if (fromCache) *fromCache = true;
                return entry->file;
            }
            cache.erase(entry);
            break;
        }
    }

    std::shared_ptr<const ImportedFile> imported;
    std::vector<fs::path> opened;
    try
    {
        imported = std::make_shared<const ImportedFile>(readFile(file, *format, options, opened));
    }
    catch (const ImportError&)
    {
        throw;
    }
    catch (const std::bad_alloc&)
    {
        throw ImportError("There is not enough memory to import " + file.filename().string() + ".");
    }
    catch (const std::exception& error)
    {
        throw ImportError(std::string("The file could not be imported: ") + error.what());
    }
    catch (...)
    {
        throw ImportError("The file could not be imported.");
    }

    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        // What the other files were when the read finished, to compare with at the next hit.
        std::vector<Dependency> dependencies;
        for (const fs::path& path : opened)
        {
            std::error_code stampError;
            Dependency dependency{path, fs::file_size(path, stampError), 0};
            dependency.modified = static_cast<long long>(fs::last_write_time(path, stampError).time_since_epoch().count());
            // A file already gone counts as changed, so it cannot be a hit.
            if (stampError) dependency.size = static_cast<std::uintmax_t>(-1);
            dependencies.push_back(std::move(dependency));
        }
        cache.push_front({key, imported, std::move(dependencies)});
        while (cache.size() > kCacheCapacity)
            cache.pop_back();
    }
    return imported;
}

} // namespace helix::import
