#include "Engine/Export/ObjWriter.h"
#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Mesh.h"
#include <cmath>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

namespace helix::exporter {

namespace {

// A float as text that reads back to the same float, whatever locale the process is in (snprintf
// follows the C locale, which a program can change, so the point is written by hand).
void appendNumber(std::string& line, float value)
{
    char buffer[32];
    const int length = std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(value));
    for (int index = 0; index < length; ++index)
        line += buffer[index] == ',' ? '.' : buffer[index];
}

// A record is one line, so a path with a space or a line break cannot be a group name as it is.
std::string groupName(const std::string& path)
{
    std::string name;
    for (const char c : path)
        name += (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? '_' : c;
    return name.empty() ? "mesh" : name;
}

} // namespace

ObjStats writeObj(const NodePacket& packet, std::ostream& out)
{
    ObjStats stats;
    std::vector<std::shared_ptr<const geo::Mesh>> meshes;
    for (const auto& primitive : packet.getPrimitives())
        if (auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(primitive)) meshes.push_back(std::move(mesh));

    out << "# Written by Helix\n";
    // The indices of an obj file count on from the first record of the file, all meshes together.
    std::uint64_t pointBase = 0;
    std::uint64_t normalBase = 0;
    std::uint64_t uvBase = 0;
    std::string line;
    for (const auto& mesh : meshes)
    {
        if (meshes.size() > 1) out << "g " << groupName(mesh->getPath()) << '\n';

        // The index in the file of each point offset, 0 for a point that was deleted.
        const auto positions = mesh->pointPosSpan();
        const std::size_t pointSlots = positions.size();
        std::vector<std::uint64_t> fileIndex(pointSlots, 0);
        std::uint64_t written = 0;
        for (std::size_t point = 0; point < pointSlots; ++point)
        {
            if (!mesh->isValidPoint(point)) continue;
            const Vector3& position = positions[point];
            if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(position.z()))
            {
                stats.error = "A point has a position that is not a number, which an obj file cannot hold.";
                return stats;
            }
            line = "v ";
            appendNumber(line, position.x());
            line += ' ';
            appendNumber(line, position.y());
            line += ' ';
            appendNumber(line, position.z());
            line += '\n';
            out << line;
            fileIndex[point] = pointBase + (++written);
        }

        const Offset vertexCount = mesh->getNumVerts();
        const Offset faceCount = mesh->getNumFaces();
        // Normals and texture coordinates go per vertex, as the engine keeps them, so a face names
        // the same index for its point, its normal and its coordinate.
        const bool withNormals = faceCount > 0 && vertexCount > 0;
        const auto uvAttribute = mesh->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::uv);
        const bool withUvs = uvAttribute && uvAttribute->getType() == attr::AttributeType::vectorT &&
                             uvAttribute->getSize() == vertexCount && vertexCount > 0;
        if (withNormals)
        {
            const geo::VertexNormalHandle normals = mesh->getVertexNormal();
            for (Offset vertex = 0; vertex < vertexCount; ++vertex)
            {
                const Vector3 normal = normals[vertex];
                line = "vn ";
                appendNumber(line, std::isfinite(normal.x()) ? normal.x() : 0.f);
                line += ' ';
                appendNumber(line, std::isfinite(normal.y()) ? normal.y() : 0.f);
                line += ' ';
                appendNumber(line, std::isfinite(normal.z()) ? normal.z() : 0.f);
                line += '\n';
                out << line;
            }
            stats.hasNormals = true;
        }
        if (withUvs)
        {
            const attr::AttributeHandleRO<Vector3> uvs(uvAttribute);
            for (Offset vertex = 0; vertex < vertexCount; ++vertex)
            {
                const Vector3 uv = uvs.getValue(vertex);
                line = "vt ";
                appendNumber(line, std::isfinite(uv.x()) ? uv.x() : 0.f);
                line += ' ';
                appendNumber(line, std::isfinite(uv.y()) ? uv.y() : 0.f);
                line += '\n';
                out << line;
            }
            stats.hasUvs = true;
        }

        const auto starts = mesh->getFaceStartVertices();
        for (Offset face = 0; face < faceCount; ++face)
        {
            if (!mesh->isValidFace(face) || face >= starts.size()) continue;
            const Offset first = starts[face];
            const unsigned int corners = mesh->getFaceVertCount(face);
            const bool closed = mesh->isClosed(face);
            const bool polygon = closed && corners >= 3;
            if (!polygon && corners < 2) continue;

            line = polygon ? "f" : "l";
            bool complete = true;
            for (unsigned int corner = 0; corner < corners; ++corner)
            {
                const Offset vertex = first + corner;
                const Offset point = mesh->getVertexPoint(vertex);
                if (point >= pointSlots || fileIndex[point] == 0)
                {
                    complete = false;
                    break;
                }
                line += ' ';
                line += std::to_string(fileIndex[point]);
                // A line takes no normal, only a polygon does.
                if (polygon && (withUvs || withNormals))
                {
                    line += '/';
                    if (withUvs) line += std::to_string(uvBase + vertex + 1);
                    if (withNormals) line += '/' + std::to_string(normalBase + vertex + 1);
                }
            }
            if (!complete) continue;
            line += '\n';
            out << line;
            if (polygon) ++stats.faces;
            else ++stats.lines;
        }

        pointBase += written;
        if (withNormals) normalBase += vertexCount;
        if (withUvs) uvBase += vertexCount;
        stats.points += written;
    }
    return stats;
}

} // namespace helix::exporter
