#include "Engine/Simulation/SpringSolver.h"
#include <algorithm>
#include <cmath>

namespace helix::sim {

namespace {

// Mixes a value into a running hash, the same mix the engine's expressions
// use, so the lattice starts from the same place every time.
uint32_t hashCombine(uint32_t hash, uint32_t value)
{
    hash = (hash ^ value) + 0x9e3779b9u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

// Returns a number in [0, 1) from a hash.
floatT hashToUnit(uint32_t hash) { return static_cast<floatT>(hash >> 8) / 16777216.0f; }

} // namespace

void SpringSolver::reset(const SpringSettings& settings)
{
    settings_ = settings;

    const intT columns = std::max<intT>(1, settings.columns);
    const intT rows = std::max<intT>(1, settings.rows);
    settings_.columns = columns;
    settings_.rows = rows;

    positions_.clear();
    previousPositions_.clear();
    springs_.clear();
    restLengths_.clear();

    positions_.reserve(static_cast<std::size_t>(columns * rows));
    previousPositions_.reserve(static_cast<std::size_t>(columns * rows));

    // The lattice hangs as a sheet in the plane of the screen, with its top
    // row held in place.
    for (intT row = 0; row < rows; ++row)
    {
        for (intT column = 0; column < columns; ++column)
        {
            const floatT jitter = hashToUnit(hashCombine(
                hashCombine(0x811c9dc5u, static_cast<uint32_t>(settings.seed)),
                static_cast<uint32_t>(row * columns + column)
            )) - 0.5f;

            const Vector3 position(
                settings.origin.x() + static_cast<floatT>(column) * settings.spacing,
                settings.origin.y() - static_cast<floatT>(row) * settings.spacing,
                settings.origin.z() + jitter * settings.spacing * 0.01f
            );
            positions_.push_back(position);
            previousPositions_.push_back(position);
        }
    }

    // The springs hold the points together: across, down, across the corners,
    // and every second point along, which keeps the sheet from folding on
    // itself.
    const auto addSpring = [this](intT from, intT to) {
        springs_.emplace_back(from, to);
        restLengths_.push_back(restLength_(from, to));
    };

    for (intT row = 0; row < rows; ++row)
    {
        for (intT column = 0; column < columns; ++column)
        {
            const intT index = indexAt(column, row);
            if (column + 1 < columns) addSpring(index, indexAt(column + 1, row));
            if (row + 1 < rows) addSpring(index, indexAt(column, row + 1));
            if (column + 1 < columns && row + 1 < rows)
                addSpring(index, indexAt(column + 1, row + 1));
            if (column > 0 && row + 1 < rows) addSpring(index, indexAt(column - 1, row + 1));
            if (column + 2 < columns) addSpring(index, indexAt(column + 2, row));
            if (row + 2 < rows) addSpring(index, indexAt(column, row + 2));
        }
    }
}

floatT SpringSolver::restLength_(intT fromIndex, intT toIndex) const
{
    if (fromIndex < 0 || toIndex < 0) return 0.f;
    if (static_cast<std::size_t>(fromIndex) >= positions_.size()) return 0.f;
    if (static_cast<std::size_t>(toIndex) >= positions_.size()) return 0.f;
    return (positions_[static_cast<std::size_t>(toIndex)] - positions_[static_cast<std::size_t>(fromIndex)])
        .norm();
}

void SpringSolver::step(const SpringSettings& settings, floatT deltaTime)
{
    if (deltaTime <= 0.f) return;
    if (positions_.empty()) return;

    const floatT damping = std::clamp(settings.damping, 0.f, 1.f);
    const floatT squaredTime = deltaTime * deltaTime;

    // The top row is held in place, so the lattice hangs from it.
    const intT pinnedCount = std::max<intT>(1, settings_.columns);

    for (std::size_t index = 0; index < positions_.size(); ++index)
    {
        if (static_cast<intT>(index) < pinnedCount) continue;

        const Vector3& position = positions_[index];
        const Vector3& previous = previousPositions_[index];

        // The Verlet step works the new position out from where the point was
        // and where it came from, which keeps the movement steady.
        const Vector3 movement = (position - previous) * damping;
        const Vector3 acceleration = settings.gravity;
        const Vector3 newPosition = position + movement + acceleration * squaredTime;

        previousPositions_[index] = position;
        positions_[index] = newPosition;
    }

    satisfySprings_(settings);

    // The held row goes back to where it started, so it never drifts.
    for (intT index = 0; index < pinnedCount; ++index)
        positions_[static_cast<std::size_t>(index)] = previousPositions_[static_cast<std::size_t>(index)];
}

void SpringSolver::satisfySprings_(const SpringSettings& settings)
{
    const floatT stiffness = std::clamp(settings.stiffness, 0.f, 1.f);
    const intT iterations = std::max<intT>(1, settings.iterations);
    const intT pinnedCount = std::max<intT>(1, settings_.columns);

    for (intT iteration = 0; iteration < iterations; ++iteration)
    {
        for (std::size_t springIndex = 0; springIndex < springs_.size(); ++springIndex)
        {
            const intT fromIndex = springs_[springIndex].first;
            const intT toIndex = springs_[springIndex].second;
            const floatT restLength = restLengths_[springIndex];
            if (restLength <= 0.f) continue;

            const bool fromIsPinned = fromIndex < pinnedCount;
            const bool toIsPinned = toIndex < pinnedCount;
            if (fromIsPinned && toIsPinned) continue;

            Vector3& from = positions_[static_cast<std::size_t>(fromIndex)];
            Vector3& to = positions_[static_cast<std::size_t>(toIndex)];

            const Vector3 difference = to - from;
            const floatT length = difference.norm();
            if (length <= 1e-6f) continue;

            // The pull each end takes, split between them unless one of them
            // is held in place.
            const Vector3 correction = difference * ((length - restLength) / length) * stiffness;
            if (fromIsPinned) to -= correction;
            else if (toIsPinned) from += correction;
            else
            {
                from += correction * 0.5f;
                to -= correction * 0.5f;
            }
        }
    }
}

std::vector<Vector3> simulateSprings(
    const SpringSettings& startSettings,
    const std::function<SpringSettings(floatT frame)>& settingsAtFrame,
    floatT startFrame,
    floatT endFrame,
    floatT fps,
    intT subSteps
)
{
    SpringSolver solver;
    solver.reset(startSettings);

    if (endFrame <= startFrame) return solver.getPositions();
    if (fps <= 0.f) return solver.getPositions();

    const intT steps = std::max<intT>(1, subSteps);
    const floatT frameDelta = 1.f / fps;
    const floatT stepTime = frameDelta / static_cast<floatT>(steps);

    // The lattice is always run from the frame it starts on, so the state at a
    // frame does not depend on how the scene got there.
    const floatT frameCount = endFrame - startFrame;
    const intT wholeFrames = static_cast<intT>(std::floor(frameCount + 1e-4f));

    for (intT frameIndex = 0; frameIndex < wholeFrames; ++frameIndex)
    {
        const floatT frame = startFrame + static_cast<floatT>(frameIndex);
        const SpringSettings settings = settingsAtFrame ? settingsAtFrame(frame) : startSettings;

        for (intT stepIndex = 0; stepIndex < steps; ++stepIndex)
            solver.step(settings, stepTime);
    }

    // The frame the scene sits on may sit between two whole frames, so the run
    // ends with a fraction of a frame. Without it, scrubbing to a half frame
    // would give the state of the frame below it.
    const floatT remainder = frameCount - static_cast<floatT>(wholeFrames);
    if (remainder > 1e-4f)
    {
        const floatT frame = startFrame + static_cast<floatT>(wholeFrames);
        const SpringSettings settings = settingsAtFrame ? settingsAtFrame(frame) : startSettings;

        const floatT partialStepTime = stepTime * remainder;
        for (intT stepIndex = 0; stepIndex < steps; ++stepIndex)
            solver.step(settings, partialStepTime);
    }

    return solver.getPositions();
}

} // namespace helix::sim
