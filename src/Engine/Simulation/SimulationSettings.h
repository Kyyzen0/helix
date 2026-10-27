#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "Engine/Simulation/ParticleSolver.h"
#include "Engine/Simulation/SpringSolver.h"
#include <string>

namespace helix::sim {

/**
 * @brief Reads the settings of a simulation at a frame, keyframes included.
 *
 * The helpers here are what a node calls to gather its settings. Every value
 * is read through the keyframe store, so a setting that holds keyframes gives
 * the value its track holds at that frame, and any other setting gives the
 * value it holds now.
 *
 * @note A header rather than a source file, so a node can use it without the
 * engine library having to know about it.
 */
namespace detail {

/// @brief Returns the value of one component of a parameter at a frame.
inline floatT parameterAt(
    nt::NodeId nodeId,
    const std::string& parameter,
    unsigned int component,
    floatT frame
)
{
    return resolveParameter(nodeId, parameter, component, frame);
}

/// @brief Returns the three components of a vector parameter at a frame.
inline Vector3 vectorAt(
    nt::NodeId nodeId,
    const std::string& parameter,
    floatT frame
)
{
    return Vector3(
        parameterAt(nodeId, parameter, 0, frame),
        parameterAt(nodeId, parameter, 1, frame),
        parameterAt(nodeId, parameter, 2, frame)
    );
}

} // namespace detail

/**
 * @brief Builds the settings of a particle system at a frame.
 *
 * @param nodeId The node the settings come from.
 * @param frame The frame to read the settings at.
 * @return The settings the particle system runs with at that frame.
 */
inline ParticleSettings particleSettingsAt(nt::NodeId nodeId, floatT frame)
{
    ParticleSettings settings;
    settings.count = static_cast<intT>(detail::parameterAt(nodeId, "count", 0, frame));
    settings.origin = detail::vectorAt(nodeId, "origin", frame);
    settings.extent = detail::vectorAt(nodeId, "extent", frame);
    settings.gravity = detail::vectorAt(nodeId, "gravity", frame);
    settings.drag = detail::parameterAt(nodeId, "drag", 0, frame);
    settings.swirl = detail::parameterAt(nodeId, "swirl", 0, frame);
    settings.seed = static_cast<intT>(detail::parameterAt(nodeId, "seed", 0, frame));
    return settings;
}

/**
 * @brief Builds the settings of a spring sheet at a frame.
 *
 * @param nodeId The node the settings come from.
 * @param frame The frame to read the settings at.
 * @return The settings the spring sheet runs with at that frame.
 */
inline SpringSettings springSettingsAt(nt::NodeId nodeId, floatT frame)
{
    SpringSettings settings;
    settings.columns = static_cast<intT>(detail::parameterAt(nodeId, "columns", 0, frame));
    settings.rows = static_cast<intT>(detail::parameterAt(nodeId, "rows", 0, frame));
    settings.spacing = detail::parameterAt(nodeId, "spacing", 0, frame);
    settings.origin = detail::vectorAt(nodeId, "origin", frame);
    settings.gravity = detail::vectorAt(nodeId, "gravity", frame);
    settings.stiffness = detail::parameterAt(nodeId, "stiffness", 0, frame);
    settings.damping = detail::parameterAt(nodeId, "damping", 0, frame);
    settings.iterations = static_cast<intT>(detail::parameterAt(nodeId, "iterations", 0, frame));
    settings.seed = static_cast<intT>(detail::parameterAt(nodeId, "seed", 0, frame));
    return settings;
}

} // namespace helix::sim
