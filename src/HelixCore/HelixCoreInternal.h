#pragma once

/**
 * @file HelixCoreInternal.h
 * @brief What the files behind the facade share. Not part of the facade: a Swift application
 * never imports this header.
 */

#include "HelixCore/HelixCore.h"
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace helix {
class NodePacket;
}

namespace helix::core::detail {

inline Status success() { return Status{true, {}}; }

inline Status failure(std::string message) { return Status{false, std::move(message)}; }

/// @brief Runs an engine call, turning whatever it throws into a failed status.
inline Status guarded(const std::function<Status()>& action)
{
    try
    {
        return action();
    }
    catch (const std::exception& error)
    {
        return failure(error.what());
    }
    catch (...)
    {
        return failure("The engine failed for an unknown reason.");
    }
}

/// @brief Cooks the display node on the calling thread and returns what it produced.
Status cookDisplayPacket(std::shared_ptr<const NodePacket>& packet);

} // namespace helix::core::detail
