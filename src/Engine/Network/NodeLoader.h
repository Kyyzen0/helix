#pragma once
#include <boost/config.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace enzo::script {
class PointScript;
}

namespace enzo::nt {

/**
 * @brief The startup pass that fills the node type table from the node folders on disk.
 *
 * Every node is a folder holding a node.yaml, and the builtin nodes load
 * through the same path an installed one does. Every folder is read up front so
 * the tab menu has every node's label the moment it opens, while a node's
 * shared library only opens once a node from it is loaded.
 *
 * A user authored node is a folder of the same shape, holding a node.yaml and
 * the source the node runs. The user nodes directory is read the same way as
 * the shipped one, so a node the user wrote is registered, named, and saved
 * with a project the same as a shipped one.
 *
 * @note Nodes whose implementation cannot be resolved are reported and skipped.
 */
class BOOST_SYMBOL_EXPORT NodeLoader
{
  public:
    /// @brief Reads every node folder and registers what it finds.
    /// @note Does nothing when called again.
    static void loadNodes();

    /// @brief Returns the directory the node folders live in.
    /// @throws std::runtime_error when no candidate directory exists.
    static std::filesystem::path getNodesDirectory();

    /**
     * @brief Returns the directory the user's own node folders live in.
     *
     * The directory holds one folder per user node, each with a node.yaml and
     * the source the node runs. It sits beside the project's other user data,
     * so a node the user wrote is still there the next time the application
     * starts.
     *
     * @note The environment variable ENZO_USER_NODES_DIR overrides the
     * directory, which lets a test and a development run point somewhere else.
     */
    static std::filesystem::path getUserNodesDirectory();

    /**
     * @brief Registers the user node in a folder, compiling the source it names.
     *
     * The folder holds a node.yaml whose implementation names the file holding
     * the node's source. The source is compiled and the node's type is added to
     * the node type table, which is what puts the node in the tab menu.
     *
     * @param folder The folder holding the node's manifest and source.
     * @param error The reason the node was not registered, empty when it was.
     * @return True when the node was registered.
     * @note A node whose name is already taken has its source replaced instead
     * of adding a second node of that name. The parameters of a node are fixed
     * when it is first registered, so a changed manifest needs a new name.
     */
    static bool registerUserNode(const std::filesystem::path& folder, std::string& error);

    /**
     * @brief Returns the compiled source behind a user node.
     *
     * @param folder The folder the node was loaded from.
     * @return The compiled source, or null when the node has none.
     */
    static std::shared_ptr<const script::PointScript>
    getUserScript(const std::filesystem::path& folder);
};

} // namespace enzo::nt
