#pragma once
#include "Engine/Core/Path.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/CookContext.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Parameter/NodeParameter.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace helix::nt {
/**
 * @class Node
 * @brief The unique runtime representation of a node
 */
class Node
{
  public:
    /**
     * @brief Constructs a new node
     *
     * @param nodeId the node id assigned to this node. For most situations
     * this should be set by the nt::NetworkManager
     * @param nodeType The data class informing the node what its properties
     * are that set it apart from other nodes. This is what makes a grid
     * node different to a transform node.
     * @param path The full path locating the node, whose leaf is the node name.
     * The nt::NetworkManager picks a path that is free within its parent.
     */
    Node(helix::nt::NodeId nodeId, const nt::NodeType& nodeType, const Path& path);
    virtual ~Node() = default;
    /// @brief Deleted copy constructor to avoid accidental copies.
    Node(const Node&) = delete;
    /// @brief Deleted copy assignment operator to avoid accidental copies.
    Node& operator=(const Node&) = delete;

    /// @brief Builds the node's implementation and runs its cook, filling the output packets.
    /// @note Does nothing while the node is clean.
    ///
    /// The cook starts from a clean slate: the error and the warnings of the last cook are
    /// forgotten and every output goes back to an empty packet, so a node that fails (or that
    /// leaves an output unset) never hands the geometry of an older cook to what is downstream.
    /// An exception thrown by the implementation does not leave this function. It is recorded as
    /// the node's error, the outputs are left empty, and the cook of the rest of the graph goes
    /// on with an empty input where this node was.
    void cook(nt::CookContext& context);

    /// @brief The error the last cook of this node ended with, empty when it cooked cleanly or
    /// has not cooked since it was last changed.
    std::string getError() const;

    /// @brief What the last cook of this node warned about, in the order it said it.
    std::vector<std::string> getWarnings() const;

    /// @brief Notes that the cook failed. Only the first error of a cook is kept: nodes stop
    /// at the first thing that is wrong, and a later one is a consequence of it.
    /// @note Called from the node's implementation (NodeImpl::throwError), so it can run on a
    /// worker thread of the background cook while the interface reads the node.
    void recordError(std::string error);

    /// @brief Notes something the cook went on in spite of.
    void recordWarning(std::string warning);

    /**
     * @brief Returns the current output geometry.
     *
     * Does not trigger a cook so if the geometry may be outdated if not cooked first.
     *
     * @todo Add option to force cook or cook if dirty.
     */
    std::shared_ptr<const helix::NodePacket> getOutputPacket(unsigned int outputIndex) const;

    /// @brief Stores finished geometry against one of the node's outputs.
    void setOutputPacket(unsigned int outputIndex, helix::NodePacket packet);

    /// @brief Returns whether anything downstream is asking for one of the node's outputs.
    /// @todo Always true until dirtiness is tracked per output.
    bool outputRequested(unsigned int outputIndex) const;

    /// @brief Returns all parameters belonging to this node.
    std::vector<std::weak_ptr<prm::NodeParameter>> getParameters();

    /// @brief Returns a parameter with the given name belonging to this node.
    /// @returns Empty default constructed std::weak_ptr<prm::Parameter>() if no parameter of that
    /// name exists.
    std::weak_ptr<prm::NodeParameter> getParameter(std::string_view parameterName);

    /// @brief Whether the named parameter's disable condition leaves it enabled.
    /// @return True when the condition is empty, unparsable, or not met.
    bool isParameterEnabled(std::string_view parmName);

    /// @brief Whether the named parameter's hide condition currently hides it.
    /// @return True only when the condition is present and met.
    bool isParameterHidden(std::string_view parmName);

    /// @brief Whether a template's hide condition currently hides it.
    /// @note Unlike isParameterHidden this takes any template of the node, so a group or a
    /// divider (which are not parameters) can be asked too. Ancestors are not consulted: a
    /// caller that draws groups hides what is inside a hidden group itself.
    bool isTemplateHidden(const prm::Template& templateEntry);

    /// @brief Whether a template's disable condition leaves it enabled. See isTemplateHidden.
    bool isTemplateEnabled(const prm::Template& templateEntry);

    /// @brief Returns the value text written as prm(name) reads from that
    /// parameter, or the text itself when it names no parameter.
    /// @return Nothing when the text names a parameter this node does not have.
    std::optional<std::string> getReferencedValue(const std::string& text);

    /// @brief Returns the template tree declared by this node's type.
    /// @returns Top level templates. Group templates contain child templates recursively.
    const std::vector<prm::Template>& getTemplates() const;

    /**
     * @brief Returns the runtime name uniquely identifying this node among its siblings (eg.
     * "grid1")
     *
     * The name is the leaf of the node's path, not stored separately. Unlike the type name it is
     * per node and is intended to be user assignable. Only nodes sharing a parent have to differ,
     * so the same name can appear once in every scope.
     */
    std::string getName() const;

    /// @brief Returns the full path locating this node within the network.
    const Path& getPath() const { return path_; }

    /// @brief Sets the full path locating this node within the network.
    void setPath(const Path& path) { path_ = path; }

    /**
     * @brief Returns the static type definition this node was created from.
     *
     * The type carries data shared by every node of the same type such as its
     * name and label. Use getType().getName() for the internal type name (eg.
     * "copy_to_points") and getType().getLabel() for the display label (eg. "Copy To Points").
     */
    const nt::NodeType& getType() const;

    /**
     * @brief Marks the outputed geometry as outdated and notifies the network
     *
     * @param dirtyDescendents Sets whether all descendents (nodes connected
     * directly or indirectly to the output of this node) are also dirtied.
     * This is usually what you want.
     */
    void dirtyNode(bool dirtyDescendents = true);

    /// @brief Returns true if the node is dirty and false if the node is clean (does not need
    /// cooking).
    bool isDirty();

    /// @brief Returns a number that changes every time the node cooks, and is never the same for
    /// two cooks of any node of the process. Zero means the node has not cooked.
    ///
    /// A reader that keeps a copy of what the node produced (a table of its attributes) compares
    /// the number it saw to this one to know the copy is out of date. The number is drawn from
    /// one counter shared by every node, so a node that takes the place of another after a
    /// scene is restored cannot be mistaken for the one it replaces.
    std::uint64_t getCookGeneration() const { return cookGeneration_.load(std::memory_order_relaxed); }

    /// @brief Returns whether the node takes any input at all.
    bool takesInput() const;

    /// @brief Returns the number of available outputs the node provides.
    unsigned int getMaxOutputs() const;

    /// @brief Returns the node's position in the network graph.
    Vector2 getPosition() const { return position_; }
    /// @brief Sets the node's position in the network graph.
    void setPosition(Vector2 pos) { position_ = pos; }

    /// @brief A signal emitted when the node is dirtied. This will usually notify the
    /// NetworkManager
    boost::signals2::signal<void(nt::NodeId nodeId, bool dirtyDescendents)> nodeDirtied;

    /// @brief A signal emitted when one parameter's value changes, carrying its name.
    boost::signals2::signal<void(const std::string& parmName)> parameterChanged;

  private:
    void initParameters();

    /// @brief Forgets the error and the warnings, which describe a cook that is over.
    void clearDiagnostics();

    /// @brief Puts every output back to an empty packet.
    void resetOutputPackets();

    /// @brief Whether a disable or hide comparison currently evaluates true.
    /// @return True only when the comparison is present, parses, and is met.
    bool isComparisonTrue(const std::string& conditionText);

    /// @brief Notifies observers a parameter changed and dirties the node.
    void onParameterChanged(const std::string& parmName);

    std::vector<std::shared_ptr<prm::NodeParameter>> parameters_;
    std::vector<std::shared_ptr<const helix::NodePacket>> outputPackets_;
    // What the last cook said about itself. A cook can run on a worker thread of the background
    // pool while the interface reads these, so they have a lock of their own.
    mutable std::mutex diagnosticsMutex_;
    // The mark of the last cook, see getCookGeneration. Atomic since a worker thread cooks.
    std::atomic<std::uint64_t> cookGeneration_{0};
    std::string error_;
    std::vector<std::string> warnings_;
    helix::nt::NodeId nodeId_;
    const helix::nt::NodeType& nodeType_;
    // Full path locating this node within the network, its leaf is the node name
    Path path_;
    Vector2 position_{0.f, 0.f};
    bool dirty_ = true;
};
} // namespace helix::nt
