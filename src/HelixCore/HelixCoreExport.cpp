// The half of the facade that writes geometry out of the application. See the Export section of
// HelixCore.h.

#include "Engine/Export/ObjWriter.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Mesh.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <system_error>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

GeometryExportResult exportGeometry(const std::string& path)
{
    namespace fs = std::filesystem;
    GeometryExportResult result;
    result.status = guarded([&] {
        if (path.empty()) return failure("There is no path to write to.");
        std::shared_ptr<const NodePacket> packet;
        if (Status status = detail::cookDisplayPacket(packet); !status.ok) return status;
        bool anyMesh = false;
        for (const auto& primitive : packet->getPrimitives())
            if (std::dynamic_pointer_cast<const geo::Mesh>(primitive)) anyMesh = true;
        if (!anyMesh) return failure("The display node produced no mesh to export.");

        const fs::path destination(path);
        const fs::path directory = destination.has_parent_path() ? destination.parent_path() : fs::path(".");
        std::error_code ignored;
        if (!fs::is_directory(directory, ignored))
            return failure("The folder " + directory.string() + " does not exist.");
        if (fs::is_directory(destination, ignored))
            return failure(destination.string() + " is a folder, not a file.");

        // Written next to the destination and renamed over it, so a failure half way never leaves a
        // broken file in the place of a good one.
        static std::atomic<unsigned> counter{0};
        const fs::path temporary = directory / (destination.filename().string() + ".tmp" + std::to_string(counter++));
        exporter::ObjStats stats;
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return failure("Couldn't write to " + destination.string() + ".");
            file.imbue(std::locale::classic());
            stats = exporter::writeObj(*packet, file);
            file.flush();
            if (stats.error.empty() && !file) stats.error = "Couldn't write to " + destination.string() + ", the disk may be full.";
        }
        if (!stats.error.empty())
        {
            fs::remove(temporary, ignored);
            return failure(stats.error);
        }
        std::error_code renamed;
        fs::rename(temporary, destination, renamed);
        if (renamed)
        {
            fs::remove(temporary, ignored);
            return failure("Couldn't write to " + destination.string() + ": " + renamed.message() + ".");
        }
        result.pointCount = stats.points;
        result.faceCount = stats.faces;
        result.lineCount = stats.lines;
        return success();
    });
    return result;
}

} // namespace helix::core
