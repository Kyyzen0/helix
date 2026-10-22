#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Primitives/Mesh.h"
#include <memory>
#include <mutex>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

namespace enzo::script {
class PointScript;
class ScriptContext;
} // namespace enzo::script

namespace enzo {

/// @brief The number of points one thread takes at a time. A whole multiple of 64
/// keeps two threads off the same word of a bool attribute.
inline constexpr Offset scriptPointsPerChunk = 1024;

static_assert(scriptPointsPerChunk % 64 == 0);

/**
 * @brief Runs a compiled script over every point of every mesh in the context's input, in parallel.
 *
 * @note The output needs to start as a copy of the input.
 * @return The first error a mesh or a point produced, or an empty string.
 */
inline String runOverPoints(
    const script::PointScript& script,
    const script::ScriptContext& context,
    NodePacket& output
)
{
    using namespace enzo;

    // Clones the script for each thread, since one script runs a single point at a time.
    tbb::enumerable_thread_specific<std::shared_ptr<script::PointScript>> threadScripts(
        [&script] { return script.clone(); }
    );

    std::mutex errorMutex;
    String error;

    for (size_t primitiveIndex = 0; primitiveIndex < output.getPrimitives().size(); ++primitiveIndex)
    {
        const std::shared_ptr<geo::Primitive> outputPrim = output.getPrimitive(primitiveIndex);
        if (outputPrim->getType() != geo::PrimType::MESH) continue;
        const auto outputMesh = std::static_pointer_cast<geo::Mesh>(outputPrim);

        if (!script.addWrittenAttributes(*outputMesh, error)) return error;

        const Offset offsetCount = outputMesh->getNumPoints();
        const Offset chunkCount = (offsetCount + scriptPointsPerChunk - 1) / scriptPointsPerChunk;

        tbb::parallel_for(Offset(0), chunkCount, [&](const Offset chunk) {
            const std::shared_ptr<script::PointScript>& threadScript = threadScripts.local();

            String chunkError;
            if (!threadScript)
                chunkError = "The script's globals failed to initialise.";
            else
            {
                const Offset begin = chunk * scriptPointsPerChunk;
                const Offset end = std::min(begin + scriptPointsPerChunk, offsetCount);
                threadScript->run(context, primitiveIndex, *outputMesh, begin, end, chunkError);
            }

            if (chunkError.empty()) return;

            std::lock_guard lock(errorMutex);
            if (error.empty()) error = chunkError;
        });

        if (!error.empty()) return error;
    }

    return error;
}

} // namespace enzo
