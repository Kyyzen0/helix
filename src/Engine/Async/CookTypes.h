#pragma once

#include "Engine/Core/Types.h"

#include <atomic>
#include <cstdint>

namespace enzo::async {

/**
 * @brief Names one attempt at cooking the network.
 *
 * Every request for a cook takes the next number, so a cook can tell at any
 * moment whether it is still the one the network wants. A number is never
 * reused, so a stale cook can always be told apart from a current one.
 */
using CookGeneration = std::uint64_t;

/// @brief The generation that means no cook has ever run.
inline constexpr CookGeneration kNoGeneration = 0;

/**
 * @brief How a cook ended.
 */
enum class CookStatus
{
    /// @brief The cook has not finished yet.
    Running,

    /// @brief The cook finished and its result is ready to publish.
    Success,

    /// @brief A newer cook took over before this one finished, so its result was dropped.
    Cancelled,

    /// @brief The cook stopped because a node raised an error.
    Failed
};

/**
 * @brief A shared flag a cook polls to learn whether it should stop.
 *
 * The token is shared with the cook, so whoever asks for a new cook can raise
 * the flag without waiting for the running one to notice, and the running one
 * stops at the next point where it checks.
 *
 * @note The flag only says "stop soon". A node that is already cooking runs to
 * its end, because a node cook cannot be unwound part way through.
 */
class CookCancelToken
{
  public:
    CookCancelToken() = default;

    /// @brief Returns whether the cook should stop as soon as it can.
    bool isCancelled() const { return cancelled_.load(std::memory_order_acquire); }

    /// @brief Asks the cook to stop.
    void cancel() { cancelled_.store(true, std::memory_order_release); }

  private:
    std::atomic<bool> cancelled_{false};
};

} // namespace enzo::async
