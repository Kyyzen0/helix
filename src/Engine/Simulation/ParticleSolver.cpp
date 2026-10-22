#include "Engine/Simulation/ParticleSolver.h"
#include <algorithm>
#include <cmath>

namespace enzo::sim {

namespace {

// Mixes a value into a running hash so nearby inputs give unrelated outputs.
// The same mix the engine's expressions use, so the starting positions of the
// particles are as repeatable as everything else in the engine.
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

// Returns the hash of a seed and an index, so every particle gets its own
// starting spot without a random source.
uint32_t hashParticle(intT seed, intT index)
{
    uint32_t hash = hashCombine(0x811c9dc5u, static_cast<uint32_t>(seed));
    hash = hashCombine(hash, static_cast<uint32_t>(index));
    return hash;
}

// Returns a number in [0, 1) from a hash, since a float holds 24 bits exactly.
floatT hashToUnit(uint32_t hash) { return static_cast<floatT>(hash >> 8) / 16777216.0f; }

// Returns a number in [-1, 1) from a hash.
floatT hashToSigned(uint32_t hash) { return hashToUnit(hash) * 2.f - 1.f; }

// The push the vortex around the origin gives a particle.
Vector3 swirlForce(const Vector3& position, floatT swirl)
{
    if (swirl == 0.f) return Vector3::Zero();

    // The tangent of the circle the particle sits on, which turns the particle
    // around the origin rather than pulling it in.
    const Vector3 tangent(-position.z(), 0.f, position.x());
    const floatT length = tangent.norm();
    if (length <= 0.f) return Vector3::Zero();
    return tangent / length * swirl;
}

} // namespace

void ParticleSolver::reset(const ParticleSettings& settings)
{
    const intT count = std::max<intT>(0, settings.count);
    particles_.clear();
    particles_.reserve(static_cast<std::size_t>(count));

    for (intT index = 0; index < count; ++index)
    {
        const uint32_t hash = hashParticle(settings.seed, index);

        // Each particle starts somewhere in the box, worked out from its own
        // hash, so the starting pattern never changes between runs.
        Particle particle;
        particle.position = Vector3(
            settings.origin.x() + hashToSigned(hashCombine(hash, 1u)) * settings.extent.x() * 0.5f,
            settings.origin.y() + hashToSigned(hashCombine(hash, 2u)) * settings.extent.y() * 0.5f,
            settings.origin.z() + hashToSigned(hashCombine(hash, 3u)) * settings.extent.z() * 0.5f
        );
        particle.velocity = Vector3::Zero();
        particles_.push_back(particle);
    }
}

void ParticleSolver::step(const ParticleSettings& settings, floatT deltaTime)
{
    if (deltaTime <= 0.f) return;

    // The speed that is lost every step, kept inside the range a step can
    // take away entirely.
    const floatT drag = std::clamp(settings.drag, 0.f, 1.f);
    const floatT keep = 1.f - drag;

    for (Particle& particle : particles_)
    {
        // The acceleration is the pull plus the push of the vortex.
        const Vector3 acceleration = settings.gravity + swirlForce(particle.position, settings.swirl);

        // A step that moves the speed first and then the position keeps the
        // particles from sinking through the floor when they move fast.
        particle.velocity += acceleration * deltaTime;
        particle.velocity *= keep;
        particle.position += particle.velocity * deltaTime;
    }
}

std::vector<Particle> simulateParticles(
    const ParticleSettings& startSettings,
    const std::function<ParticleSettings(floatT frame)>& settingsAtFrame,
    floatT startFrame,
    floatT endFrame,
    floatT fps,
    intT subSteps
)
{
    ParticleSolver solver;
    solver.reset(startSettings);

    if (endFrame <= startFrame) return solver.getParticles();
    if (fps <= 0.f) return solver.getParticles();

    const intT steps = std::max<intT>(1, subSteps);
    const floatT frameDelta = 1.f / fps;
    const floatT stepTime = frameDelta / static_cast<floatT>(steps);

    // The system is always run from the frame it starts on, so the state at a
    // frame does not depend on how the scene got there.
    const intT frameCount = static_cast<intT>(std::floor(endFrame - startFrame + 1e-4f));
    for (intT frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        const floatT frame = startFrame + static_cast<floatT>(frameIndex);
        const ParticleSettings settings =
            settingsAtFrame ? settingsAtFrame(frame) : startSettings;

        for (intT stepIndex = 0; stepIndex < steps; ++stepIndex)
            solver.step(settings, stepTime);
    }

    return solver.getParticles();
}

} // namespace enzo::sim
