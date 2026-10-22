#include "Engine/Network/NetworkManager.h"
#include "Engine/Async/CookPlan.h"
#include "Engine/Async/CookResult.h"
#include "Engine/Async/CookSnapshot.h"
#include "Engine/Async/CookTypes.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkPath.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Network/UpdateLock.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Ramp.h"
#include "Engine/Serializer/ParameterSerializable.h"
#include "Engine/UndoRedo/ChangeNodeLinkCommand.h"
#include "Engine/UndoRedo/CreateNodeCommand.h"
#include "Engine/UndoRedo/DeleteNodeCommand.h"
#include "Engine/UndoRedo/MoveNodeCommand.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace enzo {

nt::NodeId nt::NetworkManager::createNode(
    const std::string& typeName,
    const Path& parent,
    const std::string& name,
    Vector2 position
)
{
    const NodeAlias* alias = NodeTypeTable::getNodeAlias(typeName);
    const NodeType& nodeType =
        NodeTypeTable::requireNodeType(alias ? alias->aliasedType : typeName);

    // An unnamed node is numbered from its type name, so the first grid becomes "grid1"
    Path path = parent.append(Path(name.empty() ? nodeType.internalName + "1" : name));
    while (getNodeAtPath(path))
        path = path.increment();

    NodeId nodeId = network_.reserveNodeId();
    createNodeWithId(nodeId, nodeType, path, position);

    if (alias) applySerializable(getNode(nodeId), alias->parameterValues);

    return nodeId;
}

void nt::NetworkManager::moveNode(NodeId nodeId, Vector2 newPos, bool skipUndo)
{
    Vector2 oldPos = getNode(nodeId).getPosition();
    getNode(nodeId).setPosition(newPos);

    if (!skipUndo)
    {
        auto cmd = std::make_unique<MoveNodeCommand>(nodeId, oldPos, newPos);
        undoStack_.push(std::move(cmd));
    }

    nodePositionChanged(nodeId, newPos);
}

void nt::NetworkManager::deleteNode(NodeId nodeId)
{
    if (!isValidNode(nodeId)) return;

    // A worker may be cooking this node, so the cook is abandoned and the
    // workers are given a moment to leave before the node goes away.
    if (cookSchedulerPtr_)
    {
        cookSchedulerPtr_->cancel();
        waitForCookIdle_();
    }

    auto updateLock = lockUpdates();

    // Group everything this delete touches into one atomic undo unit
    UndoTransaction transaction(undoStack_);

    // A node holding a scope takes its contents with it. Children are recorded first
    // so undo brings the container back before the nodes that live inside it.
    for (NodeId child : getChildNodeIds(getNode(nodeId).getPath()))
        deleteNode(child);

    // Disconnect first so the reconnects replay after the node is restored on undo
    disconnectNode(nodeId);

    auto cmd = std::make_unique<DeleteNodeCommand>(nodeId);
    undoStack_.push(std::move(cmd));

    // Release the display, primary, and selection state pointing at this node
    if (displayNode_ == nodeId) clearDisplayFlag();
    if (primaryNode_ == nodeId) clearPrimaryNode();
    setSelectedNode(nodeId, false, true);

    // Signal before erasing so listeners can still query the node
    nodeRemoved(nodeId);

    network_.deleteNode(nodeId);
}

void nt::NetworkManager::createNodeWithId(
    NodeId nodeId,
    const nt::NodeType& nodeType,
    const Path& path,
    Vector2 position
)
{
    const Path scope = path.getParent();
    if (!getScope(scope))
        throw std::out_of_range("no scope at " + scope.getString() + " to create a node in\n");

    Node& node = network_.createNode(nodeId, nodeType, path);
    node.setPosition(position);
    node.nodeDirtied.connect([this](nt::NodeId nodeId, bool dirtyDependents) {
        onNodeDirtied(nodeId, dirtyDependents);
    });

    auto cmd = std::make_unique<CreateNodeCommand>(nodeId);
    undoStack_.push(std::move(cmd));

    nodeCreated(nodeId);
}

void nt::NetworkManager::disconnectNode(NodeId nodeId)
{
    if (!isValidNode(nodeId)) return;

    // getInputs and getOutputs return copies, so disconnecting while iterating is safe
    // Disconnects from the highest input down, so closing a gap never moves one still to come
    const std::vector<nt::NodeLink> inputs = graph().getInputs(nodeId);
    for (auto input = inputs.rbegin(); input != inputs.rend(); ++input)
    {
        disconnectNodes(*input);
    }

    for (const nt::NodeLink& nodeLink : graph().getOutputs(nodeId))
    {
        disconnectNodes(nodeLink);
    }
}

nt::NetworkManager& nt::NetworkManager::getInstance()
{
    static nt::NetworkManager instance;
    return instance;
}

nt::Node& nt::NetworkManager::getNode(nt::NodeId nodeId) { return network_.getNode(nodeId); }

bool nt::NetworkManager::isValidNode(nt::NodeId nodeId) { return network_.isValidNode(nodeId); }

void nt::NetworkManager::setDisplayNode(NodeId nodeId)
{
    displayNode_ = nodeId;

    if (asyncCookingEnabled_)
    {
        // The geometry arrives when the cook is published, so the interface
        // keeps showing what it has until then.
        requestCook({nodeId});
        displayNodeChanged(nodeId);
        return;
    }

    cook(nodeId);

    nt::Node& displayNode = getNode(nodeId);
    displayGeoChanged(displayNode.getOutputPacket(0));
    displayNodeChanged(nodeId);
}

void nt::NetworkManager::clearDisplayFlag()
{
    displayNode_.reset();
    displayGeoChanged(std::make_shared<const NodePacket>());
    displayNodeChanged(std::nullopt);
}

std::optional<nt::NodeId> nt::NetworkManager::getPrimaryNode() { return primaryNode_; }

void nt::NetworkManager::setPrimaryNode(NodeId nodeId)
{
    primaryNode_ = nodeId;

    if (asyncCookingEnabled_)
    {
        requestCook({nodeId});
        primaryNodeChanged(nodeId);
        return;
    }

    cook(nodeId);

    nt::Node& primaryNode = getNode(nodeId);
    primaryGeoChanged(primaryNode.getOutputPacket(0));
    primaryNodeChanged(nodeId);
}

void nt::NetworkManager::clearPrimaryNode()
{
    primaryNode_.reset();
    primaryGeoChanged(std::make_shared<const NodePacket>());
    primaryNodeChanged(std::nullopt);
}

void nt::NetworkManager::setFrame(floatT frame)
{
    const floatT clamped =
        std::clamp(frame, static_cast<floatT>(startFrame_), static_cast<floatT>(endFrame_));
    if (clamped == frame_) return;

    frame_ = clamped;
    frameChanged(frame_);
    dirtyTimeDependents_();
}

void nt::NetworkManager::setStartFrame(intT frame)
{
    startFrame_ = frame;
    endFrame_ = std::max(frame, endFrame_);
    frameRangeChanged(startFrame_, endFrame_);

    // The frame may now sit outside the range, so put it back inside.
    setFrame(frame_);
}

void nt::NetworkManager::setEndFrame(intT frame)
{
    endFrame_ = frame;
    startFrame_ = std::min(frame, startFrame_);
    frameRangeChanged(startFrame_, endFrame_);

    setFrame(frame_);
}

void nt::NetworkManager::setFps(floatT fps)
{
    if (fps <= 0) return;

    fps_ = fps;
    fpsChanged(fps_);
}

void nt::NetworkManager::resetTime_()
{
    frame_ = 1;
    startFrame_ = 1;
    endFrame_ = 240;
    fps_ = 24;

    frameRangeChanged(startFrame_, endFrame_);
    fpsChanged(fps_);
    frameChanged(frame_);
}

void nt::NetworkManager::setSelectedNode(NodeId nodeId, bool selected, bool add)
{
    if (add)
    {
        auto idIter = std::find(selectedNodes_.begin(), selectedNodes_.end(), nodeId);
        if (selected)
        {
            // skip if value is already in selected nodes
            if (idIter != selectedNodes_.end()) return;
            selectedNodes_.push_back(nodeId);
            if (asyncCookingEnabled_) requestCook({nodeId});
            else cook(nodeId);
        }
        else
        {
            // skip if value is not in selected nodes
            if (idIter == selectedNodes_.end()) return;
            selectedNodes_.erase(idIter);
        }
    }
    else
    {
        if (selected)
        {
            selectedNodes_.clear();
            selectedNodes_.push_back(nodeId);
            if (asyncCookingEnabled_) requestCook({nodeId});
            else cook(nodeId);
        }
        else
        {
            selectedNodes_.clear();
        }
    }
    selectedNodesChanged(selectedNodes_);
}

nt::UpdateLock nt::NetworkManager::lockUpdates() { return UpdateLock(); }

void nt::NetworkManager::update()
{
    // The nodes whose geometry the interface shows.
    std::vector<nt::NodeId> targets;
    if (displayNode_) targets.push_back(*displayNode_);
    if (primaryNode_) targets.push_back(*primaryNode_);
    for (nt::NodeId selectedId : selectedNodes_) targets.push_back(selectedId);

    if (asyncCookingEnabled_)
    {
        // The cook runs in the background and the geometry is put on screen
        // when it is done, so the interface keeps showing the last result.
        if (!targets.empty()) requestCook(targets);
        processPendingCooks();
        return;
    }

    // cook display node
    if (getDisplayNode().has_value())
    {

        const NodeId displayNodeId = getDisplayNode().value();
        cook(displayNodeId);

        auto& displayNode = getNode(displayNodeId);
        displayGeoChanged(displayNode.getOutputPacket(0));
    }

    // cook primary node and notify the geometry pane
    if (getPrimaryNode().has_value())
    {
        const NodeId primaryNodeId = getPrimaryNode().value();
        cook(primaryNodeId);

        auto& primaryNode = getNode(primaryNodeId);
        primaryGeoChanged(primaryNode.getOutputPacket(0));
    }

    // cook selected nodes and notify spreadsheet
    for (NodeId selectedId : selectedNodes_)
    {
        cook(selectedId);
        auto& selectedNode = getNode(selectedId);
        selectedGeoChanged(selectedNode.getOutputPacket(0));
    }
}

const std::vector<nt::NodeId>& nt::NetworkManager::getSelectedNodes() { return selectedNodes_; }

void nt::NetworkManager::setSelectedNodes(std::vector<nt::NodeId> nodeIds)
{
    selectedNodes_.clear();
    for (NodeId nodeId : nodeIds)
    {
        if (isValidNode(nodeId))
        {
            selectedNodes_.push_back(nodeId);
            if (asyncCookingEnabled_) requestCook({nodeId});
            else cook(nodeId);
        }
    }
    selectedNodesChanged(selectedNodes_);
}

void nt::NetworkManager::clear()
{
    network_.clear();
    selectedNodes_.clear();
    undoStack_.clear();
    resetTime_();
    clearDisplayFlag();
    clearPrimaryNode();
    selectedNodesChanged(selectedNodes_);
    networkCleared();
}

void nt::NetworkManager::cook(nt::NodeId nodeId)
{
    std::vector<nt::NodeId> cookOrder = graph().getCookOrder(nodeId);

    for (nt::NodeId cookNodeId : cookOrder)
    {
        nt::Node& node = getNode(cookNodeId);
        if (node.isDirty())
        {
            // Drops the last time read, so the cook records one only if it reads it again
            graph().setTimeDependent(nt::Unit{cookNodeId}, false);

            nt::CookContext context(cookNodeId, *this);
            node.cook(context);
        }
    }
}

async::CookScheduler& nt::NetworkManager::cookScheduler()
{
    if (!cookSchedulerPtr_)
    {
        cookSchedulerPtr_ = std::make_unique<async::CookScheduler>();

        // The pool finishes a cook on a worker thread, so the result is left
        // for the interface to pick up rather than published there.
        cookSchedulerPtr_->setCookFinishedFn(
            [this](async::CookGeneration generation, async::CookStatus status) {
                std::lock_guard<std::mutex> lock(cookMutex_);
                finishedCooks_.emplace_back(generation, status);
            }
        );
    }

    return *cookSchedulerPtr_;
}

void nt::NetworkManager::requestCook(const std::vector<enzo::nt::NodeId>& targets)
{
    {
        std::lock_guard<std::mutex> lock(cookMutex_);

        // Only the newest request survives, so a burst of edits leaves one cook
        // behind rather than a queue of them.
        requestedCookTargets_ = targets;
        cookRequested_ = true;

        // The cook that is running is about to be abandoned. The nodes it has
        // touched were cooked with the values as they were before the edit, so
        // the cook that follows cooks them again. A cook that already finished
        // is left alone, since its geometry is still right.
        const bool liveCookIsRunning = cookScheduler().generation() == activeCookGeneration_;
        if (liveCookIsRunning && activeCookResult_)
            for (nt::NodeId nodeId : activeCookResult_->cookedNodes())
                nodesToRecook_.insert(nodeId);
    }

    // The cook that is running is abandoned rather than queued behind. The pool
    // takes the new cook as soon as a worker is free.
    cookScheduler().cancel();

    startPendingCook_();
}

void nt::NetworkManager::cancelCook()
{
    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        cookRequested_ = false;
        requestedCookTargets_.clear();
    }

    cookScheduler().cancel();
}

bool nt::NetworkManager::isCookBusy() const
{
    if (!cookSchedulerPtr_) return false;
    return cookSchedulerPtr_->isBusy();
}

bool nt::NetworkManager::isCooking() const
{
    if (!cookSchedulerPtr_) return false;
    return cookSchedulerPtr_->isCooking();
}

async::CookGeneration nt::NetworkManager::getCookGeneration() const
{
    if (!cookSchedulerPtr_) return async::kNoGeneration;
    return cookSchedulerPtr_->generation();
}

void nt::NetworkManager::processPendingCooks()
{
    // A cook that was asked for but could not start yet gets its turn.
    startPendingCook_();

    // The cooks that finished since the last time round.
    std::vector<std::pair<async::CookGeneration, async::CookStatus>> finished;
    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        finished.swap(finishedCooks_);
    }

    for (const auto& [generation, status] : finished)
    {
        // A cook that was replaced by a newer one has nothing to publish.
        if (status != async::CookStatus::Success) continue;
        if (generation != activeCookGeneration_) continue;

        publishCookResult_(activeCookResult_);
    }
}

void nt::NetworkManager::startPendingCook_()
{
    std::vector<nt::NodeId> targets;
    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        if (!cookRequested_) return;
        targets = requestedCookTargets_;
        cookRequested_ = false;
    }

    startCook_(targets);
}

void nt::NetworkManager::startCook_(const std::vector<nt::NodeId>& targets)
{
    if (targets.empty()) return;

    // The plan covers the targets and everything feeding them. It is built here,
    // on the thread that owns the network, so the workers never read the graph.
    async::CookPlan plan = async::CookPlan::build(
        targets,
        [this](nt::NodeId nodeId) { return graph().getInputs(nodeId); },
        [this](nt::NodeId nodeId) { return getInputCount(nodeId); }
    );

    // Only the dirty nodes need cooking. A clean node keeps the geometry it has.
    // A node an abandoned cook touched is cooked again, since that cook used the
    // values as they were before the edit.
    std::vector<std::vector<nt::NodeId>> levels;
    std::unordered_map<nt::NodeId, nt::Node*> nodesById;
    for (const std::vector<nt::NodeId>& level : plan.levels())
    {
        std::vector<nt::NodeId> dirtyInLevel;
        for (nt::NodeId nodeId : level)
        {
            nt::Node& node = getNode(nodeId);
            if (!node.isDirty() && !nodesToRecook_.count(nodeId)) continue;
            dirtyInLevel.push_back(nodeId);
            nodesById.emplace(nodeId, &node);
        }
        if (!dirtyInLevel.empty()) levels.push_back(std::move(dirtyInLevel));
    }

    // The nodes this cook covers are taken off the list. The ones it does not
    // cover stay on it, so they are cooked the next time the cook reaches them.
    for (const std::vector<nt::NodeId>& level : levels)
        for (nt::NodeId nodeId : level) nodesToRecook_.erase(nodeId);

    // Take the picture the cook reads, so it never reads the live network.
    auto snapshot = std::make_shared<async::CookSnapshot>();
    auto result = std::make_shared<async::CookResult>();
    buildSnapshot_(plan, *snapshot, *result);

    auto planPtr = std::make_shared<async::CookPlan>(std::move(plan));

    activeCookResult_ = result;
    activeCookGeneration_ = cookScheduler().requestCook(
        levels,
        [this, snapshot, planPtr, result, nodesById](
            nt::NodeId nodeId,
            const async::CookCancelToken& cancelToken
        ) {
            auto node = nodesById.find(nodeId);
            if (node == nodesById.end()) return;
            cookNodeAsync_(
                nodeId, node->second, cancelToken, *snapshot, *planPtr, *result
            );
        }
    );
}

void nt::NetworkManager::buildSnapshot_(
    const async::CookPlan& plan,
    async::CookSnapshot& snapshot,
    async::CookResult& result
)
{
    snapshot.setTime(getFrame(), getTime());

    for (nt::NodeId nodeId : plan.nodes())
    {
        nt::Node& node = getNode(nodeId);

        async::CookSnapshot::NodeState state;
        state.nodeId = nodeId;
        state.inputCount = getInputCount(nodeId);
        snapshot.setNode(state);

        // A clean node keeps the geometry it has, so the cook that reads it
        // finds it waiting in the result.
        if (!node.isDirty())
        {
            for (unsigned int outputIndex = 0; outputIndex < node.getMaxOutputs(); ++outputIndex)
                result.setOutput(nodeId, outputIndex, node.getOutputPacket(outputIndex));
            continue;
        }

        // Take the parameters the cook will read. Each component is read in
        // all three forms, because a parameter can be read as any of them and
        // one driven by an expression gives a different answer each time it
        // runs.
        for (const std::weak_ptr<prm::NodeParameter>& weakParameter : node.getParameters())
        {
            std::shared_ptr<prm::NodeParameter> parameter = weakParameter.lock();
            if (!parameter) continue;

            async::CookParameterValue value;
            value.valueType = parameter->getValueType();

            const unsigned int componentCount = parameter->getVectorSize();
            for (unsigned int component = 0; component < componentCount; ++component)
            {
                value.floats.push_back(parameter->evalFloat(component));
                value.ints.push_back(parameter->evalInt(component));
                value.strings.push_back(parameter->evalString(component));
            }

            if (parameter->getType() == prm::Type::RAMP) value.ramp = prm::Ramp(*parameter);

            snapshot.setParameter(nodeId, parameter->getName(), std::move(value));
        }
    }
}

void nt::NetworkManager::cookNodeAsync_(
    nt::NodeId nodeId,
    nt::Node* node,
    const async::CookCancelToken& cancelToken,
    const async::CookSnapshot& snapshot,
    const async::CookPlan& plan,
    async::CookResult& result
)
{
    if (!node) return;
    if (cancelToken.isCancelled()) return;

    // The node is noted as touched before it cooks, so a cook that is
    // abandoned is known to have been part way through this node.
    result.markCooked(nodeId);

    {
        nt::CookContext context(nodeId, *this, snapshot, plan, result, cancelToken);
        node->cook(context);
    }

    // A cook that was abandoned leaves the node dirty, so the cook that comes
    // after it cooks the node again with the values as they are by then.
    if (cancelToken.isCancelled()) node->dirtyNode(false);

    // File away what the node produced.
    for (unsigned int outputIndex = 0; outputIndex < node->getMaxOutputs(); ++outputIndex)
        result.setOutput(nodeId, outputIndex, node->getOutputPacket(outputIndex));
}

void nt::NetworkManager::publishCookResult_(
    const std::shared_ptr<const async::CookResult>& result
)
{
    if (!result) return;

    // The graph belongs to this thread, so the notes the cook made about the
    // scene time are applied here rather than on the worker that made them.
    for (nt::NodeId nodeId : result->cookedNodes())
        graph().setTimeDependent(nt::Unit{nodeId}, false);
    for (nt::NodeId nodeId : result->timeDependentNodes())
        graph().setTimeDependent(nt::Unit{nodeId}, true);

    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        publishedCookResult_ = result;
    }

    // Tell the interface about the new geometry. The whole cook is put on
    // screen at once, so what is shown is never half of one cook.
    if (displayNode_) displayGeoChanged(getPublishedOutputPacket(*displayNode_, 0));
    if (primaryNode_) primaryGeoChanged(getPublishedOutputPacket(*primaryNode_, 0));
    for (nt::NodeId selectedId : selectedNodes_)
        selectedGeoChanged(getPublishedOutputPacket(selectedId, 0));
}

std::shared_ptr<const async::CookResult> nt::NetworkManager::getPublishedCookResult() const
{
    std::lock_guard<std::mutex> lock(cookMutex_);
    return publishedCookResult_;
}

std::shared_ptr<const enzo::NodePacket> nt::NetworkManager::getPublishedOutputPacket(
    enzo::nt::NodeId nodeId,
    unsigned int outputIndex
)
{
    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        if (publishedCookResult_)
        {
            std::shared_ptr<const NodePacket> packet =
                publishedCookResult_->getOutput(nodeId, outputIndex);
            if (packet) return packet;
        }
    }

    return getNode(nodeId).getOutputPacket(outputIndex);
}

void nt::NetworkManager::waitForCookIdle_()
{
    if (!cookSchedulerPtr_) return;
    cookSchedulerPtr_->waitForIdle();
}

unsigned int nt::NetworkManager::getInputCount(NodeId nodeId)
{
    const nt::NodeType& nodeType = getNode(nodeId).getType();
    const unsigned int singlePortCount = nodeType.getSinglePortCount();
    if (!nodeType.hasMultiInputPort()) return singlePortCount;

    unsigned int multiNodeLinkCount = 0;
    for (const nt::NodeLink& nodeLink : graph().getInputs(nodeId))
        if (nodeType.isMultiInputPortAt(nodeLink.targetInput)) ++multiNodeLinkCount;

    return singlePortCount + multiNodeLinkCount;
}

nt::NodeLink nt::NetworkManager::connectNodes(
    NodeId inputNodeId,
    unsigned int inputIndex,
    NodeId outputNodeId,
    unsigned int outputIndex
)
{
    auto updateLock = lockUpdates();

    unsigned int targetInput = outputIndex;

    if (getNode(outputNodeId).getType().isMultiInputPortAt(targetInput))
    {
        // Clamps an index past the last input so the node link appends
        targetInput = std::min(targetInput, getInputCount(outputNodeId));
        openInputGap(outputNodeId, targetInput);
    }
    else if (auto existing = graph().getInputNodeLink(outputNodeId, targetInput))
    {
        // Replaces the one node link a single input port holds
        disconnectNodes(*existing);
    }

    nt::NodeLink nodeLink{inputNodeId, inputIndex, outputNodeId, targetInput};

    graph().connect(nodeLink);
    getNode(outputNodeId).dirtyNode();
    nodeLinkCreated(nodeLink);

    auto cmd = std::make_unique<ChangeNodeLinkCommand>(
        nodeLink.sourceNode,
        nodeLink.sourceOutput,
        nodeLink.targetNode,
        nodeLink.targetInput,
        ChangeNodeLinkCommand::Action::Connect
    );
    undoStack_.push(std::move(cmd));

    return nodeLink;
}

void nt::NetworkManager::disconnectNodes(const nt::NodeLink& nodeLink)
{
    auto cmd = std::make_unique<ChangeNodeLinkCommand>(
        nodeLink.sourceNode,
        nodeLink.sourceOutput,
        nodeLink.targetNode,
        nodeLink.targetInput,
        ChangeNodeLinkCommand::Action::Disconnect
    );
    undoStack_.push(std::move(cmd));

    graph().disconnect(nodeLink);

    const bool targetNodeExists = isValidNode(nodeLink.targetNode);

    // Only the downstream node goes stale, its input changed
    if (targetNodeExists)
    {
        getNode(nodeLink.targetNode).dirtyNode();
    }

    nodeLinkRemoved(nodeLink);

    const bool leftAMultiInputPort =
        targetNodeExists &&
        getNode(nodeLink.targetNode).getType().isMultiInputPortAt(nodeLink.targetInput);
    if (leftAMultiInputPort)
    {
        closeInputGap(nodeLink.targetNode, nodeLink.targetInput);
    }
}

void nt::NetworkManager::openInputGap(NodeId nodeId, unsigned int fromIndex)
{
    // Moves the highest input first, so no two node links ever share an index
    const std::vector<nt::NodeLink> inputs = graph().getInputs(nodeId);
    for (auto input = inputs.rbegin(); input != inputs.rend(); ++input)
    {
        const nt::NodeLink& nodeLink = *input;
        if (nodeLink.targetInput >= fromIndex) moveInput(nodeLink, nodeLink.targetInput + 1);
    }
}

void nt::NetworkManager::closeInputGap(NodeId nodeId, unsigned int fromIndex)
{
    // Moves in index order, so each node link lands on an index just vacated
    for (const nt::NodeLink& nodeLink : graph().getInputs(nodeId))
    {
        if (nodeLink.targetInput > fromIndex) moveInput(nodeLink, nodeLink.targetInput - 1);
    }
}

void nt::NetworkManager::moveInput(const nt::NodeLink& nodeLink, unsigned int inputIndex)
{
    nt::NodeLink moved = nodeLink;
    moved.targetInput = inputIndex;

    graph().disconnect(nodeLink);
    graph().connect(moved);

    nodeLinkRemoved(nodeLink);
    nodeLinkCreated(moved);
}

std::optional<nt::NodeId> nt::NetworkManager::getDisplayNode() { return displayNode_; }

void nt::NetworkManager::dirtyUnits_(const std::vector<nt::Unit>& units)
{
    for (const nt::Unit& unit : units)
    {
        nt::Node& node = getNode(unit.nodeId);
        node.dirtyNode(false);
        if (unit.isParameter()) node.parameterChanged(unit.parm);
    }
}

void nt::NetworkManager::dirtyTimeDependents_()
{
    // Holds one lock around the whole set, so the cook runs once for all of them
    auto updateLock = lockUpdates();

    dirtyUnits_(graph().getTimeDependents());
}

void nt::NetworkManager::onNodeDirtied(nt::NodeId nodeId, bool dirtyDependents)
{
    if (!dirtyDependents) return;

    dirtyUnits_(graph().getDependents(nt::Unit{nodeId}));

    if (nt::UpdateLock::isUnlocked())
    {
        update();
    }
}

void nt::NetworkManager::_reset()
{
    std::cout << "resetting network manager\n";

    // The network is about to change shape, so no worker may be inside it.
    if (cookSchedulerPtr_)
    {
        cookSchedulerPtr_->cancel();
        waitForCookIdle_();
    }

    {
        std::lock_guard<std::mutex> lock(cookMutex_);
        cookRequested_ = false;
        requestedCookTargets_.clear();
        finishedCooks_.clear();
        nodesToRecook_.clear();
        activeCookResult_.reset();
        activeCookGeneration_ = async::kNoGeneration;
        publishedCookResult_.reset();
    }

    network_.clear();
    displayNode_.reset();
    resetTime_();
}

} // namespace enzo
