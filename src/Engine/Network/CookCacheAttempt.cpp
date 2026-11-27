#include "Engine/Network/CookCacheAttempt.h"
#include "Engine/Network/CookCache.h"
#include "Engine/Network/CookKey.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/NetworkGraph/NetworkGraph.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Ramp.h"
#include "Engine/Simulation/KeyframeStore.h"
#include <exception>
#include <memory>

namespace helix::nt {

namespace {

// Stands for an input with nothing wired into it, which no packet can be mistaken for.
constexpr std::uint64_t kNoInput = ~std::uint64_t{0};

// Adds every value of a parameter the cook can read to the keys.
//
// Values that belong to a keyframed component go to `variable` instead of `stable`. A simulation
// reads such a component through its keyframes, not through the value the component holds at the
// frame, so the saved states of its solvers must not change with that value (only with the keys,
// which are added to `stable` as a whole).
void hashParameter(
    prm::NodeParameter& parameter,
    NodeId nodeId,
    const sim::KeyframeStore& keyframes,
    KeyHasher& stable,
    KeyHasher& variable
)
{
    stable.add(parameter.getName());

    // A ramp is read as a curve, whatever the control points it is made of.
    if (parameter.getType() == prm::Type::RAMP)
    {
        const prm::Ramp ramp(parameter);
        stable.add(static_cast<std::uint64_t>(ramp.size()));
        for (std::size_t index = 0; index < ramp.size(); ++index)
        {
            const prm::Ramp::Key& key = ramp.key(index);
            stable.add(key.position);
            stable.add(key.value);
            stable.add(static_cast<int>(key.interp));
        }
        return;
    }

    // Any other list of instances is not something a cook can read by name.
    if (parameter.getTemplate().isMultiParm())
    {
        stable.add(static_cast<std::uint64_t>(parameter.getInstanceCount()));
        return;
    }

    const unsigned int componentCount = parameter.getVectorSize();
    for (unsigned int component = 0; component < componentCount; ++component)
    {
        KeyHasher& target =
            keyframes.hasTrack({nodeId, parameter.getName(), component}) ? variable : stable;
        target.add(component);

        // A literal is read back in the one form it is stored in. An expression gives a value
        // that may differ with the form it is asked for, so every form is read.
        const bool driven = parameter.hasExpression(component);
        target.add(driven);

        const bool allForms = driven;
        if (allForms || parameter.getValueType() == prm::ValueType::Float)
            target.add(parameter.evalFloat(component));
        if (allForms || parameter.getValueType() == prm::ValueType::Int)
            target.add(parameter.evalInt(component));
        if (allForms || parameter.getValueType() == prm::ValueType::String)
            target.add(std::string_view(parameter.evalString(component)));
    }
}

} // namespace

std::optional<CookCacheAttempt> CookCacheAttempt::begin(Node& node, CookContext& context)
{
    if (!cookCache().enabled()) return std::nullopt;
    if (!context.readsLiveNetwork()) return std::nullopt;

    try
    {
        CookCacheAttempt attempt;
        attempt.cacheable_ = node.getType().cacheable;

        // What feeds the node: whether any of it moves with the time, and whether the cache may
        // keep what is made of it.
        const unsigned int inputCount = context.getInputCount();
        for (unsigned int inputIndex = 0; inputIndex < inputCount; ++inputIndex)
        {
            const Node* source = context.inputSourceNode(inputIndex);
            if (!source) continue;
            const Node::CacheState state = source->getCacheState();
            if (!state.varying) continue;
            attempt.inputsVary_ = true;
            if (!state.cacheable) attempt.cacheable_ = false;
        }

        // A node that does not move with the time is kept by the node itself, so a node that was
        // never seen reading the time is cooked at once, and its keys are worked out after the
        // cook if it turns out to.
        const bool mayVary = node.readsTime() || attempt.inputsVary_ ||
                             context.manager().graph().hasTimeDependentParameter(context.getNodeId());
        if (mayVary) attempt.computeKeys_(node, context);
        return attempt;
    }
    catch (const std::exception&)
    {
        // A value that cannot be read leaves the node without the cache, and the cook that
        // follows reports the problem itself.
        return std::nullopt;
    }
}

void CookCacheAttempt::computeKeys_(Node& node, CookContext& context)
{
    NetworkManager& manager = context.manager();
    const NodeId nodeId = context.getNodeId();

    KeyHasher stable;
    KeyHasher variable;
    stable.add(node.getType().getFullName());

    // What arrives at every input. The packet is the one the node it comes from published, so a
    // cook with the same packets at its inputs reads the same geometry.
    const unsigned int inputCount = context.getInputCount();
    stable.add(inputCount);
    for (unsigned int inputIndex = 0; inputIndex < inputCount; ++inputIndex)
    {
        if (!context.hasInput(inputIndex))
        {
            stable.add(kNoInput);
            continue;
        }

        // Reading the packet cooks what feeds the input first, so the identity is current.
        stable.add(context.getInputPacket(inputIndex)->identity());
    }

    const sim::KeyframeStore& keyframes = sim::KeyframeStore::instance();
    for (const std::weak_ptr<prm::NodeParameter>& weakParameter : node.getParameters())
    {
        const std::shared_ptr<prm::NodeParameter> parameter = weakParameter.lock();
        if (!parameter || parameter->getType() == prm::Type::DIVIDER) continue;
        hashParameter(*parameter, nodeId, keyframes, stable, variable);
    }
    stable.add(keyframes.keyOf(nodeId));

    // The frame rate changes what a solver steps by, so it is part of what a saved state depends
    // on, not only of one frame.
    stable.add(manager.getFps());
    stateKey_ = stable.value();
    variableKey_ = variable.value();
    keysReady_ = true;
}

std::uint64_t CookCacheAttempt::frameKey_(CookContext& context) const
{
    NetworkManager& manager = context.manager();
    KeyHasher key;
    key.add(stateKey_);
    key.add(variableKey_);
    key.add(manager.getFrame());
    key.add(manager.getTime());
    return key.value();
}

bool CookCacheAttempt::tryRestore(Node& node, CookContext& context)
{
    if (!cacheable_ || !keysReady_) return false;

    const std::shared_ptr<const CachedCook> cached =
        cookCache().findFrame(context.getNodeId(), frameKey_(context));
    if (!cached || cached->outputs.size() != node.getMaxOutputs()) return false;

    node.restoreCachedCook(*cached);
    node.setCacheState(Node::CacheState{true, true});

    // Moving the scene to another frame has to keep reaching the node, which only happens
    // while the graph knows the node reads the time. A cook that did not run did not tell it.
    if (cached->readsTime) context.noteTimeDependency();
    return true;
}

void CookCacheAttempt::finish(Node& node, CookContext& context)
{
    try
    {
        const bool varying = node.readsTime() || inputsVary_ ||
                             context.manager().graph().hasTimeDependentParameter(context.getNodeId());
        node.setCacheState(Node::CacheState{varying, cacheable_});

        // Nothing is kept of a cook that failed or was abandoned, and what does not move with
        // the time is kept by the node itself.
        if (!varying || !cacheable_ || context.isCancelled() || !node.getError().empty()) return;

        // A node that has just been found to move with the time was cooked without its keys. Its
        // inputs and its parameters are what they were when the cook began.
        if (!keysReady_) computeKeys_(node, context);

        auto cook = std::make_shared<CachedCook>();
        cook->outputs = node.getOutputPackets();
        cook->warnings = node.getWarnings();
        cook->readsTime = node.readsTime();
        cook->frame = context.manager().getFrame();

        std::size_t bytes = sizeof(CachedCook);
        for (const std::shared_ptr<const NodePacket>& packet : cook->outputs)
            if (packet) bytes += packet->estimateBytes();

        cookCache().storeFrame(context.getNodeId(), frameKey_(context), std::move(cook), bytes);
    }
    catch (const std::exception&)
    {
        // The cook worked, only the cache missed out.
    }
}

} // namespace helix::nt
