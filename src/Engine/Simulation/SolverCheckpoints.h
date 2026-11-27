#pragma once
#include "Engine/Core/Types.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace helix::sim {

/**
 * @brief Lets a simulation start from the state of a whole frame, and leave states behind.
 *
 * A solver is always worked out from the frame it starts on, so reaching frame 500 costs 500
 * frames of steps. With checkpoints, a run asks for the latest state it was handed before and
 * only steps from there, and hands over its own state every few frames so a later run can start
 * from it. The result is the same, bit for bit, as the run from the first frame: the state is the
 * whole of what the next step reads, and the steps are taken in the same order.
 *
 * The solver counts whole frames from the frame it starts on: index 0 is the starting state and
 * index N the state after N whole frames. What the states are filed under (the settings they
 * were made with, the node they belong to) is up to whoever provides the checkpoints.
 *
 * @tparam State What a solver keeps between two steps.
 */
template <class State> struct SolverCheckpoints
{
    /// @brief Returns the state at the highest index not above `maxIndex`, with that index.
    /// @note Left empty, or returning nothing, the run starts from the first frame.
    std::function<std::optional<std::pair<std::int64_t, std::shared_ptr<const State>>>(std::int64_t maxIndex)>
        find;

    /// @brief Receives the state after `index` whole frames. The state is only valid during the call.
    /// @note Left empty, the run leaves nothing behind.
    std::function<void(std::int64_t index, const State& state)> save;

    /// @brief How many whole frames lie between two saved states. A run saves at the multiples.
    /// @note Saving costs a copy of the state, and a later run steps at most this many frames from
    /// the nearest one, so a few frames is the balance for a state of any size.
    std::int64_t interval = 8;
};

} // namespace helix::sim
