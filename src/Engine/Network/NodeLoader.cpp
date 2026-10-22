#include "Engine/Network/NodeLoader.h"
#include "Engine/Core/InstallPaths.h"
#include "Engine/Network/NodeManifest.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Script/PointScript.h"
#include "Engine/Script/ScriptEngine.h"
#include <boost/dll/shared_library.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// The source and build tree locations, which let a development run work with no install step.
#ifndef ENZO_DEV_NODES_DIR
#define ENZO_DEV_NODES_DIR ""
#endif
#ifndef ENZO_DEV_LIB_DIR
#define ENZO_DEV_LIB_DIR ""
#endif

namespace enzo::nt {

namespace {

// The file naming a folder as a node.
constexpr const char* kManifestName = "node.yaml";

std::filesystem::path getLibraryFile(const std::string& libraryName)
{
    // TODO: add env var finder
    // TODO: add the node's own bin directory
    const std::string fileName = boost::dll::shared_library::decorate(libraryName).string();

    const std::filesystem::path installed = getInstallRoot() / "lib" / fileName;
    if (std::filesystem::exists(installed)) return installed;

    const std::filesystem::path developmentDirectory(ENZO_DEV_LIB_DIR);
    const std::filesystem::path development = developmentDirectory / fileName;
    if (!developmentDirectory.empty() && std::filesystem::exists(development)) return development;

    throw std::runtime_error(
        "Couldn't find library " + libraryName + ", tried " + installed.string() + " and " +
        development.string()
    );
}

boost::dll::shared_library& openLibrary(const std::string& libraryName)
{
    static std::map<std::string, boost::dll::shared_library> openLibraries;

    auto found = openLibraries.find(libraryName);
    if (found != openLibraries.end()) return found->second;

    const std::filesystem::path path = getLibraryFile(libraryName);
    auto inserted = openLibraries.emplace(
        libraryName,
        boost::dll::shared_library(path.string(), boost::dll::load_mode::default_mode)
    );
    return inserted.first->second;
}

// Returns a node's constructor from the library its manifest names.
nodeConstructor getConstructor(const std::string& libraryName, const std::string& constructorName)
{
    boost::dll::shared_library& library = openLibrary(libraryName);
    const std::string symbol = nodeConstructorSymbol(constructorName);

    if (!library.has(symbol))
        throw std::runtime_error(
            "library " + libraryName + " exports no constructor named " + constructorName
        );

    return &library.get<NodeImpl*(Node&, CookContext&)>(symbol);
}

// A node folder on disk and the manifest read from it.
struct NodeFolder
{
    std::filesystem::path path;
    NodeManifest manifest;
};

void reportSkippedNode(const std::filesystem::path& folder, const std::exception& error)
{
    std::cerr << "Couldn't load node " << folder.string() << ", " << error.what() << "\n";
}

// Returns every node folder with its manifest. A folder that fails to read is
// reported and skipped.
std::vector<NodeFolder> readNodeFolders()
{
    std::vector<NodeFolder> nodeFolders;
    for (const auto& entry : std::filesystem::directory_iterator(NodeLoader::getNodesDirectory()))
    {
        // A node is always a folder.
        if (!entry.is_directory()) continue;

        const std::filesystem::path manifestPath = entry.path() / kManifestName;
        if (!std::filesystem::exists(manifestPath))
        {
            std::cerr << "Skipping " << entry.path().string() << ", it has no " << kManifestName
                      << "\n";
            continue;
        }

        try
        {
            nodeFolders.push_back({entry.path(), NodeManifest::loadFromFile(manifestPath)});
        }
        catch (const std::exception& error)
        {
            reportSkippedNode(entry.path(), error);
        }
    }
    return nodeFolders;
}

// The compiled source behind each user node, held against the folder the node
// was loaded from.
std::map<std::filesystem::path, std::shared_ptr<const script::PointScript>>& userScripts()
{
    static std::map<std::filesystem::path, std::shared_ptr<const script::PointScript>> scripts;
    return scripts;
}

// Returns the directory the user's own data lives in.
std::filesystem::path getUserDataDirectory()
{
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) return std::filesystem::path(home) / "Library" / "Application Support" / "Helix";
    return std::filesystem::temp_directory_path() / "Helix";
#elif defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData) return std::filesystem::path(appData) / "Helix";
    return std::filesystem::temp_directory_path() / "Helix";
#else
    const char* dataHome = std::getenv("XDG_DATA_HOME");
    if (dataHome && *dataHome) return std::filesystem::path(dataHome) / "helix";
    const char* home = std::getenv("HOME");
    if (home && *home) return std::filesystem::path(home) / ".local" / "share" / "helix";
    return std::filesystem::temp_directory_path() / "helix";
#endif
}

} // namespace

std::filesystem::path NodeLoader::getNodesDirectory()
{
    const std::filesystem::path installed = getInstallRoot() / "nodes";
    if (std::filesystem::is_directory(installed)) return installed;

    const std::filesystem::path development(ENZO_DEV_NODES_DIR);
    if (!development.empty() && std::filesystem::is_directory(development)) return development;

    throw std::runtime_error(
        "Couldn't find a nodes directory, tried " + installed.string() + " and " +
        development.string()
    );
}

std::filesystem::path NodeLoader::getUserNodesDirectory()
{
    // The environment variable lets a test and a development run keep their
    // nodes somewhere else.
    if (const char* override = std::getenv("ENZO_USER_NODES_DIR"))
        if (*override) return std::filesystem::path(override);

    return getUserDataDirectory() / "nodes";
}

bool NodeLoader::registerUserNode(const std::filesystem::path& folder, std::string& error)
{
    error.clear();

    try
    {
        const NodeManifest manifest = NodeManifest::loadFromFile(folder / kManifestName);

        const auto* implementation =
            std::get_if<ScriptImplementation>(&manifest.getImplementation());
        if (!implementation)
        {
            error = "the node in " + folder.string() + " is not a user node";
            return false;
        }

        // The source sits beside the manifest, named by the manifest.
        const std::filesystem::path sourcePath = folder / implementation->source;
        std::ifstream sourceFile(sourcePath);
        if (!sourceFile)
        {
            error = "Couldn't read the source " + sourcePath.string();
            return false;
        }

        std::stringstream source;
        source << sourceFile.rdbuf();

        // A source that does not compile leaves the node out, rather than taking
        // the application down with it.
        String compileError;
        const std::shared_ptr<const script::PointScript> script =
            script::ScriptEngine::instance().compile(source.str(), compileError);
        if (!script)
        {
            error = compileError.empty() ? "the source did not compile" : compileError;
            return false;
        }

        NodeType nodeType = manifest.getNodeType();
        nodeType.folder = folder;
        nodeType.ctorFunc = getConstructor(implementation->library, implementation->constructor);

        // The source is held against the folder, where the node finds it when
        // it cooks.
        userScripts()[folder] = script;

        // A node of that name is already registered, so its source is replaced
        // rather than a second node of that name being added. The parameters of
        // a node are fixed when it is first registered.
        if (NodeTypeTable::getNodeType(nodeType.getFullName())) return true;

        NodeTypeTable::addNodeType(std::move(nodeType));
        return true;
    }
    catch (const std::exception& caught)
    {
        error = caught.what();
        return false;
    }
}

std::shared_ptr<const script::PointScript> NodeLoader::getUserScript(
    const std::filesystem::path& folder
)
{
    const auto found = userScripts().find(folder);
    if (found == userScripts().end()) return nullptr;
    return found->second;
}

void NodeLoader::loadNodes()
{
    static bool nodesLoaded = false;
    if (nodesLoaded) return;
    nodesLoaded = true;

    const std::vector<NodeFolder> nodeFolders = readNodeFolders();

    // Nodes with their own implementation
    for (const NodeFolder& nodeFolder : nodeFolders)
    {
        const auto* implementation =
            std::get_if<CppImplementation>(&nodeFolder.manifest.getImplementation());
        if (!implementation) continue;

        try
        {
            NodeType nodeType = nodeFolder.manifest.getNodeType();
            nodeType.folder = nodeFolder.path;
            nodeType.ctorFunc = getConstructor(implementation->library, implementation->constructor);
            NodeTypeTable::addNodeType(std::move(nodeType));
        }
        catch (const std::exception& error)
        {
            reportSkippedNode(nodeFolder.path, error);
        }
    }

    // Aliases of the node types registered above
    for (const NodeFolder& nodeFolder : nodeFolders)
    {
        const auto* implementation =
            std::get_if<AliasImplementation>(&nodeFolder.manifest.getImplementation());
        if (!implementation) continue;

        try
        {
            const NodeType& aliasedType =
                NodeTypeTable::requireNodeType(implementation->aliasedType);
            NodeTypeTable::addNodeAlias(nodeFolder.manifest.getNodeAlias(aliasedType));
        }
        catch (const std::exception& error)
        {
            reportSkippedNode(nodeFolder.path, error);
        }
    }

    // The user's own nodes, read the same way as the shipped ones. A node that
    // cannot be read is reported and left out, so a broken user node never
    // stops the application from starting.
    const std::filesystem::path userDirectory = getUserNodesDirectory();
    if (!std::filesystem::is_directory(userDirectory)) return;

    for (const auto& entry : std::filesystem::directory_iterator(userDirectory))
    {
        if (!entry.is_directory()) continue;
        if (!std::filesystem::exists(entry.path() / kManifestName)) continue;

        std::string error;
        if (!registerUserNode(entry.path(), error))
            reportSkippedNode(entry.path(), std::runtime_error(error));
    }
}

} // namespace enzo::nt
