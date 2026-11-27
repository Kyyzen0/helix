// The half of the facade that reports on the geometry of a node: counts, bounds, the attribute
// list and the values of one attribute. See the Display geometry section of HelixCore.h.

#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Mesh.h"
#include "Graphics/DisplayGeometry.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <string>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

attr::AttributeOwner toEngineOwner(AttributeOwner owner)
{
    switch (owner)
    {
    case AttributeOwner::Point:
        return attr::AttributeOwner::POINT;
    case AttributeOwner::Vertex:
        return attr::AttributeOwner::VERTEX;
    case AttributeOwner::Face:
        return attr::AttributeOwner::FACE;
    case AttributeOwner::Primitive:
        return attr::AttributeOwner::PRIMITIVE;
    }
    return attr::AttributeOwner::POINT;
}

AttributeValueType toValueType(attr::AttributeType type)
{
    switch (type)
    {
    case attr::AttributeType::intT:
        return AttributeValueType::Int;
    case attr::AttributeType::floatT:
        return AttributeValueType::Float;
    case attr::AttributeType::vectorT:
        return AttributeValueType::Vector3;
    case attr::AttributeType::boolT:
        return AttributeValueType::Bool;
    case attr::AttributeType::matrixT:
        return AttributeValueType::Matrix4;
    case attr::AttributeType::listT:
        break;
    }
    throw std::runtime_error("An attribute of a type the facade cannot report.");
}

const char* ownerName(AttributeOwner owner)
{
    switch (owner)
    {
    case AttributeOwner::Point:
        return "point";
    case AttributeOwner::Vertex:
        return "vertex";
    case AttributeOwner::Face:
        return "face";
    case AttributeOwner::Primitive:
        return "primitive";
    }
    return "point";
}

// Only a mesh has vertices and faces. Every primitive has points and itself.
bool hasOwner(const geo::Primitive& primitive, AttributeOwner owner)
{
    if (owner == AttributeOwner::Vertex || owner == AttributeOwner::Face)
        return dynamic_cast<const geo::Mesh*>(&primitive) != nullptr;
    return true;
}

std::uint64_t elementCount(const geo::Primitive& primitive, AttributeOwner owner)
{
    switch (owner)
    {
    case AttributeOwner::Point:
        return primitive.getNumPoints();
    case AttributeOwner::Primitive:
        return 1;
    case AttributeOwner::Vertex:
        if (const auto* mesh = dynamic_cast<const geo::Mesh*>(&primitive)) return mesh->getNumVerts();
        return 0;
    case AttributeOwner::Face:
        if (const auto* mesh = dynamic_cast<const geo::Mesh*>(&primitive)) return mesh->getNumFaces();
        return 0;
    }
    return 0;
}

constexpr AttributeOwner allOwners[] = {
    AttributeOwner::Point,
    AttributeOwner::Vertex,
    AttributeOwner::Face,
    AttributeOwner::Primitive
};

void growBounds(Bounds& bounds, float x, float y, float z)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!bounds.valid)
    {
        bounds.valid = true;
        bounds.minX = bounds.maxX = x;
        bounds.minY = bounds.maxY = y;
        bounds.minZ = bounds.maxZ = z;
        return;
    }
    bounds.minX = std::min(bounds.minX, x);
    bounds.minY = std::min(bounds.minY, y);
    bounds.minZ = std::min(bounds.minZ, z);
    bounds.maxX = std::max(bounds.maxX, x);
    bounds.maxY = std::max(bounds.maxY, y);
    bounds.maxZ = std::max(bounds.maxZ, z);
}

template <typename T> void copyRange(
    const std::shared_ptr<const attr::Attribute>& attribute,
    std::uint64_t first,
    std::uint64_t last,
    std::vector<float>& floats,
    std::vector<std::int64_t>& ints
)
{
    const attr::AttributeHandleRO<T> handle(attribute);
    for (std::uint64_t index = first; index < last; ++index)
    {
        const T value = handle.getValue(index);
        if constexpr (std::is_same_v<T, helix::Vector3>)
        {
            floats.push_back(value.x());
            floats.push_back(value.y());
            floats.push_back(value.z());
        }
        else if constexpr (std::is_same_v<T, helix::Matrix4>)
        {
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    floats.push_back(value(row, column));
        }
        else if constexpr (std::is_same_v<T, helix::floatT>)
            floats.push_back(value);
        else
            ints.push_back(static_cast<std::int64_t>(value));
    }
}

} // namespace

GeometryReport cookNodeReport(NodeId node)
{
    // The last report made, kept with the cook it describes. A table that asks again for a node
    // that has not cooked since gets the same report without the work of making it, which for a
    // large mesh includes splitting every face into triangles to count them.
    struct Cached
    {
        NodeId node = 0;
        std::uint64_t generation = 0;
        GeometryReport report;
    };
    static Cached cached;

    GeometryReport report;
    report.status = guarded([&] {
        std::shared_ptr<const NodePacket> packet;
        if (Status status = detail::cookNodePacket(node, packet); !status.ok) return status;

        report.generation = nt::nm().getNode(node).getCookGeneration();
        if (report.generation != 0 && cached.node == node && cached.generation == report.generation)
        {
            report = cached.report;
            return success();
        }

        std::vector<const geo::Material*> seenMaterials;
        for (const auto& primitive : packet->getPrimitives())
        {
            if (!primitive) continue;
            // Where this primitive's elements start, among everything before it. Points,
            // vertices and faces are counted the way readAttribute walks them.
            PrimitiveRange range;
            range.pointStart = report.pointCount;
            range.vertexStart = report.vertexCount;
            range.faceStart = report.faceCount;
            range.pointCount = primitive->getNumPoints();
            range.vertexCount = elementCount(*primitive, AttributeOwner::Vertex);
            range.faceCount = elementCount(*primitive, AttributeOwner::Face);
            report.primitives.push_back(range);
            ++report.primitiveCount;
            report.pointCount += primitive->getNumPoints();
            if (const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(primitive))
            {
                report.faceCount += mesh->getNumFaces();
                report.vertexCount += mesh->getNumVerts();
                for (const std::shared_ptr<const geo::Material>& material : mesh->materials())
                {
                    if (!material || std::find(seenMaterials.begin(), seenMaterials.end(), material.get()) != seenMaterials.end())
                        continue;
                    seenMaterials.push_back(material.get());
                    MaterialInfo info;
                    info.name = material->name;
                    for (int channel = 0; channel < 4; ++channel) info.baseColor[channel] = material->baseColor[channel];
                    if (material->baseColorTexture)
                    {
                        info.hasTexture = true;
                        info.textureWidth = material->baseColorTexture->width;
                        info.textureHeight = material->baseColorTexture->height;
                        info.textureSource = material->baseColorTexture->source;
                    }
                    info.missingTexture = material->missingTexture;
                    report.materials.push_back(std::move(info));
                }
                const auto positions = mesh->pointPosSpan();
                for (std::size_t point = 0; point < positions.size(); ++point)
                {
                    if (!mesh->isValidPoint(point)) continue;
                    growBounds(report.bounds, positions[point].x(), positions[point].y(), positions[point].z());
                }
            }

            for (const AttributeOwner owner : allOwners)
            {
                if (!hasOwner(*primitive, owner)) continue;
                for (const auto& attribute : primitive->getAttributes(toEngineOwner(owner), true))
                {
                    const auto known = std::find_if(
                        report.attributes.begin(),
                        report.attributes.end(),
                        [&](const AttributeInfo& info) {
                            return info.owner == owner && info.name == attribute->getName();
                        }
                    );
                    if (known != report.attributes.end())
                    {
                        known->elementCount += elementCount(*primitive, owner);
                        if (known->type != toValueType(attribute->getType())
                            || known->componentCount != attribute->getTypeSize())
                            known->mixedTypes = true;
                        continue;
                    }
                    AttributeInfo info;
                    info.owner = owner;
                    info.name = attribute->getName();
                    info.type = toValueType(attribute->getType());
                    info.componentCount = attribute->getTypeSize();
                    info.elementCount = elementCount(*primitive, owner);
                    report.attributes.push_back(std::move(info));
                }
            }
        }

        gfx::DisplayGeometry geometry;
        gfx::buildDisplayGeometry(geometry, *packet);
        report.triangleCount = geometry.topology.triangleIndices.size() / 3;
        cached = {node, report.generation, report};
        return success();
    });
    return report;
}

std::uint64_t nodeCookGeneration(NodeId node)
{
    if (!detail::requireNodeInScene(node).ok) return 0;
    return nt::nm().getNode(node).getCookGeneration();
}

AttributeValues readAttribute(
    NodeId node,
    AttributeOwner owner,
    const std::string& name,
    std::uint64_t start,
    std::uint64_t count
)
{
    AttributeValues values;
    values.status = guarded([&] {
        std::shared_ptr<const NodePacket> packet;
        if (Status status = detail::cookNodePacket(node, packet); !status.ok) return status;
        values.generation = nt::nm().getNode(node).getCookGeneration();

        // The elements of every primitive that carries the attribute, one after the other.
        std::uint64_t seen = 0;
        bool found = false;
        for (const auto& primitive : packet->getPrimitives())
        {
            if (!primitive || !hasOwner(*primitive, owner)) continue;
            const std::shared_ptr<const attr::Attribute> attribute =
                static_cast<const geo::Primitive&>(*primitive).getAttribByName(toEngineOwner(owner), name, true);
            if (!attribute) continue;

            if (!found)
            {
                found = true;
                values.type = toValueType(attribute->getType());
                values.componentCount = attribute->getTypeSize();
            }
            else if (toValueType(attribute->getType()) != values.type || attribute->getTypeSize() != values.componentCount)
                return failure("The attribute '" + name + "' has different types on the primitives.");

            const std::uint64_t size = attribute->getSize();
            // The part of this primitive's run that the request covers.
            const std::uint64_t wantedFirst = std::max(start, seen);
            const std::uint64_t wantedEnd =
                count > std::numeric_limits<std::uint64_t>::max() - start ? seen + size : start + count;
            const std::uint64_t first = wantedFirst - seen;
            const std::uint64_t last = std::min(size, wantedEnd > seen ? wantedEnd - seen : 0);
            if (wantedFirst < seen + size && first < last)
            {
                switch (values.type)
                {
                case AttributeValueType::Int:
                    copyRange<helix::intT>(attribute, first, last, values.floats, values.ints);
                    break;
                case AttributeValueType::Float:
                    copyRange<helix::floatT>(attribute, first, last, values.floats, values.ints);
                    break;
                case AttributeValueType::Vector3:
                    copyRange<helix::Vector3>(attribute, first, last, values.floats, values.ints);
                    break;
                case AttributeValueType::Bool:
                    copyRange<helix::boolT>(attribute, first, last, values.floats, values.ints);
                    break;
                case AttributeValueType::Matrix4:
                    copyRange<helix::Matrix4>(attribute, first, last, values.floats, values.ints);
                    break;
                }
            }
            seen += size;
        }

        if (!found)
            return failure(
                std::string("The node has no ") + ownerName(owner) + " attribute named '" + name + "'."
            );
        values.totalCount = seen;
        values.start = std::min(start, seen);
        return success();
    });
    if (!values.status.ok)
    {
        values.floats.clear();
        values.ints.clear();
    }
    return values;
}

} // namespace helix::core
