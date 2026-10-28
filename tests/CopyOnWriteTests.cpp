#include "Engine/Attribute/Attribute.h"
#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Camera.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Script/PointScript.h"
#include "Engine/Script/ScriptContext.h"
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>
#include <vector>

using namespace helix;

namespace {

struct NMReset
{
    NMReset()
    {
        nt::nm()._reset();
        nt::NodeLoader::loadNodes();
    }
    ~NMReset() { nt::nm()._reset(); }
};

template <typename T> T makeValue(int seed)
{
    if constexpr (std::is_same_v<T, boolT>) return seed % 2 == 0;
    else if constexpr (std::is_same_v<T, Vector3>) return Vector3(seed, seed + 1, seed + 2);
    else if constexpr (std::is_same_v<T, Matrix4>) return Matrix4::Constant(seed);
    else return static_cast<T>(seed);
}

// An attribute holding makeValue(0..count-1).
template <typename T> std::shared_ptr<attr::Attribute> makeAttribute(int count)
{
    auto attribute =
        std::make_shared<attr::Attribute>("test", attr::getAttributeType<T>());
    attr::AttributeHandle<T> handle(attribute);
    for (int i = 0; i < count; ++i)
        handle.addValue(makeValue<T>(i));
    return attribute;
}

template <typename T> std::vector<T> valuesOf(const std::shared_ptr<attr::Attribute>& attribute)
{
    return attr::AttributeHandleRO<T>(attribute).getAllValues();
}

// Returns the address of the first element, or null for bools whose storage has no addresses.
template <typename T> const void* storeAddress(const std::shared_ptr<attr::Attribute>& attribute)
{
    if constexpr (std::is_same_v<T, boolT>) return nullptr;
    else return &attr::AttributeHandleRO<T>(attribute)[0];
}

// A grid of columns x rows quads.
geo::Mesh makeGrid(int columns, int rows)
{
    geo::Mesh mesh;
    std::vector<Vector3> points;
    for (int row = 0; row <= rows; ++row)
        for (int column = 0; column <= columns; ++column)
            points.emplace_back(column, 0, row);
    mesh.addPoints(points);

    std::vector<Offset> flat;
    std::vector<Offset> counts;
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const Offset first = row * (columns + 1) + column;
            flat.insert(flat.end(), {first, first + 1, first + columns + 2, first + columns + 1});
            counts.push_back(4);
        }
    }
    mesh.addFaces(flat, counts);
    return mesh;
}

std::vector<Vector3> positionsOf(const geo::Mesh& mesh)
{
    const auto span = mesh.pointPosSpan();
    return {span.begin(), span.end()};
}

// The vertexCount attribute read through its attribute, not through the mesh's own handle.
std::vector<intT> vertexCountsOf(const geo::Mesh& mesh)
{
    const auto attribute = mesh.getAttribByName(attr::AttrOwner::FACE, "vertexCount", true);
    REQUIRE(attribute);
    return attr::AttributeHandleRO<intT>(attribute).getAllValues();
}

void requireSameVectors(const std::vector<Vector3>& a, const std::vector<Vector3>& b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);
}

// Releases the threads at the same moment.
struct StartGate
{
    std::atomic<size_t> arrived{0};
    size_t total;
    explicit StartGate(size_t threadCount) : total(threadCount) {}
    void wait()
    {
        arrived.fetch_add(1);
        while (arrived.load() < total)
            std::this_thread::yield();
    }
};

} // namespace

// Attributes

TEMPLATE_TEST_CASE(
    "A copied attribute shares its values until a handle writes to it",
    "[copyOnWrite]",
    intT,
    floatT,
    Vector3,
    boolT,
    Matrix4
)
{
    const auto original = makeAttribute<TestType>(4);
    REQUIRE_FALSE(original->isStoreShared());

    const auto copy = std::make_shared<attr::Attribute>(*original);
    REQUIRE(copy->isStoreShared());
    REQUIRE(original->isStoreShared());
    REQUIRE(storeAddress<TestType>(copy) == storeAddress<TestType>(original));

    attr::AttributeHandle<TestType> writer(copy);
    writer.setValue(1, makeValue<TestType>(100));

    REQUIRE_FALSE(copy->isStoreShared());
    if constexpr (!std::is_same_v<TestType, boolT>)
        REQUIRE(storeAddress<TestType>(copy) != storeAddress<TestType>(original));
    REQUIRE(valuesOf<TestType>(original)[1] == makeValue<TestType>(1));
    REQUIRE(valuesOf<TestType>(copy)[1] == makeValue<TestType>(100));
    REQUIRE(valuesOf<TestType>(copy)[2] == makeValue<TestType>(2));
}

TEMPLATE_TEST_CASE(
    "Writing the original does not reach its copy",
    "[copyOnWrite]",
    intT,
    floatT,
    Vector3,
    boolT,
    Matrix4
)
{
    const auto original = makeAttribute<TestType>(3);
    const auto copy = std::make_shared<attr::Attribute>(*original);

    attr::AttributeHandle<TestType> writer(original);
    writer.setValue(0, makeValue<TestType>(77));

    REQUIRE(valuesOf<TestType>(copy)[0] == makeValue<TestType>(0));
    REQUIRE(valuesOf<TestType>(original)[0] == makeValue<TestType>(77));
}

TEMPLATE_TEST_CASE(
    "Two copies of one attribute stay independent",
    "[copyOnWrite]",
    intT,
    floatT,
    Vector3,
    boolT,
    Matrix4
)
{
    const auto original = makeAttribute<TestType>(3);
    const auto first = std::make_shared<attr::Attribute>(*original);
    const auto second = std::make_shared<attr::Attribute>(*original);

    attr::AttributeHandle<TestType>(first).setValue(0, makeValue<TestType>(10));
    attr::AttributeHandle<TestType>(second).setValue(0, makeValue<TestType>(20));

    REQUIRE(valuesOf<TestType>(original)[0] == makeValue<TestType>(0));
    REQUIRE(valuesOf<TestType>(first)[0] == makeValue<TestType>(10));
    REQUIRE(valuesOf<TestType>(second)[0] == makeValue<TestType>(20));
}

TEST_CASE("Resize and compact on a copy leave the original alone", "[copyOnWrite]")
{
    const auto original = makeAttribute<intT>(4);
    const auto resized = std::make_shared<attr::Attribute>(*original);
    const auto compacted = std::make_shared<attr::Attribute>(*original);

    resized->resize(6);
    compacted->compact({true, false, true, false});

    REQUIRE(valuesOf<intT>(original) == std::vector<intT>{0, 1, 2, 3});
    REQUIRE(valuesOf<intT>(resized).size() == 6);
    REQUIRE(valuesOf<intT>(resized)[3] == 3);
    REQUIRE(valuesOf<intT>(compacted) == std::vector<intT>{0, 2});
}

TEST_CASE("A read only handle never detaches", "[copyOnWrite]")
{
    const auto original = makeAttribute<floatT>(3);
    const auto copy = std::make_shared<attr::Attribute>(*original);

    const attr::AttributeHandleRO<floatT> reader(copy);
    REQUIRE(reader.getValue(2) == 2.f);
    REQUIRE(copy->isStoreShared());
    REQUIRE(storeAddress<floatT>(copy) == storeAddress<floatT>(original));
}

TEST_CASE("Threads detaching the same attribute end up on one store", "[copyOnWrite][threads]")
{
    constexpr size_t threadCount = 8;
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        const auto original = makeAttribute<Vector3>(1000);
        const auto copy = std::make_shared<attr::Attribute>(*original);

        StartGate gate(threadCount);
        std::vector<const void*> addresses(threadCount, nullptr);
        std::vector<std::thread> threads;
        for (size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&, t] {
                gate.wait();
                attr::AttributeHandle<Vector3> handle(copy);
                addresses[t] = &handle[0];
            });
        }
        for (std::thread& thread : threads)
            thread.join();

        for (const void* address : addresses)
            REQUIRE(address == addresses.front());
        REQUIRE(addresses.front() != storeAddress<Vector3>(original));
        REQUIRE_FALSE(copy->isStoreShared());
    }
}

// Meshes

TEST_CASE("A copied mesh shares its points until it is written", "[copyOnWrite]")
{
    const geo::Mesh source = makeGrid(2, 1);
    const geo::Mesh copy = source;

    REQUIRE(copy.pointPosSpan().data() == source.pointPosSpan().data());
    REQUIRE(copy.vertexPointSpan().data() == source.vertexPointSpan().data());
}

TEST_CASE("Transforming a copy leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const std::vector<Vector3> before = positionsOf(source);

    geo::Mesh copy = source;
    Matrix4 translation = Matrix4::Identity();
    translation.col(3).head<3>() = Vector3(5, 6, 7);
    copy.applyTransform(translation, TransformClass::POINT);

    requireSameVectors(positionsOf(source), before);
    REQUIRE(copy.getPointPos(0) == before[0] + Vector3(5, 6, 7));
    REQUIRE(copy.intrinsicHandlesBound());
    REQUIRE(source.getPointPos(0) == before[0]);
}

TEST_CASE("Writing the source mesh after it was copied leaves the copy unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const geo::Mesh copy = source;
    const std::vector<Vector3> before = positionsOf(copy);

    source.setPointPos(0, Vector3(9, 9, 9));
    source.addPoint(Vector3(1, 1, 1));

    requireSameVectors(positionsOf(copy), before);
    REQUIRE(source.getPointPos(0) == Vector3(9, 9, 9));
    REQUIRE(source.getNumPoints() == before.size() + 1);
    REQUIRE(source.intrinsicHandlesBound());
}

TEST_CASE("Moving one point of a copy detaches the points only once", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    geo::Mesh copy = source;

    copy.setPointPos(0, Vector3(3, 3, 3));
    const Vector3* afterFirst = copy.pointPosSpan().data();
    copy.setPointPos(1, Vector3(4, 4, 4));

    REQUIRE(copy.pointPosSpan().data() == afterFirst);
    REQUIRE(source.getPointPos(0) == Vector3(0, 0, 0));
    REQUIRE(source.getPointPos(1) == Vector3(1, 0, 0));
}

TEST_CASE("Adding points to a copy leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const std::vector<Vector3> before = positionsOf(source);
    geo::Mesh copy = source;

    const Offset added = copy.addPoint(Vector3(7, 7, 7));
    const std::vector<Vector3> more{Vector3(1, 1, 1), Vector3(2, 2, 2)};
    copy.addPoints(more);

    REQUIRE(added == before.size());
    REQUIRE(copy.getNumPoints() == before.size() + 3);
    REQUIRE(source.getNumPoints() == before.size());
    requireSameVectors(positionsOf(source), before);
    REQUIRE(copy.intrinsicHandlesBound());
}

TEST_CASE("Adding faces after a copy detached writes into the new stores", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const std::vector<intT> countsBefore = vertexCountsOf(source);
    const Offset sourceFaces = source.getNumFaces();
    const Offset sourceVerts = source.getNumVerts();

    geo::Mesh copy = source;
    const std::vector<Offset> flat{0, 1, 2, 3, 4, 5};
    const std::vector<Offset> counts{4, 2};
    copy.addFaces(flat, counts);

    REQUIRE(copy.getNumFaces() == sourceFaces + 2);
    REQUIRE(copy.getNumVerts() == sourceVerts + 6);
    REQUIRE(copy.getFaceVertCount(sourceFaces) == 4);
    REQUIRE(copy.getFaceVertCount(sourceFaces + 1) == 2);
    REQUIRE(copy.isValidFace(sourceFaces + 1));
    REQUIRE(copy.isValidVertex(sourceVerts + 5));
    REQUIRE(copy.getVertexPoint(sourceVerts + 4) == 4);

    // The values reached the attribute, not an old store the handle kept.
    const std::vector<intT> countsAfter = vertexCountsOf(copy);
    REQUIRE(countsAfter.size() == sourceFaces + 2);
    REQUIRE(countsAfter[sourceFaces] == 4);
    REQUIRE(countsAfter[sourceFaces + 1] == 2);
    REQUIRE(copy.intrinsicHandlesBound());

    REQUIRE(source.getNumFaces() == sourceFaces);
    REQUIRE(source.getNumVerts() == sourceVerts);
    REQUIRE(vertexCountsOf(source) == countsBefore);
}

TEST_CASE("Adding a single face to a copy leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    geo::Mesh copy = source;

    const Offset face = copy.addFace({0, 1, 2}, true);

    REQUIRE(copy.getFaceVertCount(face) == 3);
    REQUIRE(source.getNumFaces() == 1);
    REQUIRE(source.getNumVerts() == 4);
    REQUIRE(vertexCountsOf(source) == std::vector<intT>{4});
}

TEST_CASE("Deleting faces of a copy and defragmenting leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const std::vector<Vector3> positionsBefore = positionsOf(source);
    const Offset faces = source.getNumFaces();
    const Offset verts = source.getNumVerts();

    geo::Mesh copy = source;
    copy.deleteFaces({0}, true);
    copy.defragment();

    REQUIRE(copy.getNumFaces() == faces - 1);
    REQUIRE(copy.getNumVerts() == verts - 4);
    REQUIRE(copy.getNumPoints() == positionsBefore.size() - 2);
    REQUIRE(copy.intrinsicHandlesBound());

    REQUIRE(source.getNumFaces() == faces);
    REQUIRE(source.getNumVerts() == verts);
    requireSameVectors(positionsOf(source), positionsBefore);
    for (Offset face = 0; face < faces; ++face)
        REQUIRE(source.isValidFace(face));
}

TEST_CASE("Defragmenting a mesh with nothing deleted keeps the stores shared", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    geo::Mesh copy = source;

    copy.defragment();

    REQUIRE(copy.pointPosSpan().data() == source.pointPosSpan().data());
}

TEST_CASE("Deleting points and vertices of a copy leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    geo::Mesh viaPoints = source;
    geo::Mesh viaVertices = source;

    viaPoints.deletePoints({0}, true);
    viaVertices.deleteVertices({0, 1, 2, 3});

    for (Offset point = 0; point < source.getNumPoints(); ++point)
        REQUIRE(source.isValidPoint(point));
    for (Offset vertex = 0; vertex < source.getNumVerts(); ++vertex)
        REQUIRE(source.isValidVertex(vertex));
    for (Offset face = 0; face < source.getNumFaces(); ++face)
        REQUIRE(source.isValidFace(face));
    REQUIRE_FALSE(viaPoints.isValidPoint(0));
    REQUIRE_FALSE(viaVertices.isValidFace(0));
}

TEST_CASE("Duplicating points of a copy leaves the source mesh unchanged", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    geo::Mesh copy = source;

    const std::vector<Offset> sources{0, 1};
    const std::vector<Offset> added = copy.duplicatePoints(sources);

    REQUIRE(added.size() == 2);
    REQUIRE(copy.getNumPoints() == 6);
    REQUIRE(source.getNumPoints() == 4);
}

TEST_CASE("Other attributes of a copy detach when they grow", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    attr::AttributeHandleFloat heights = source.addAttribute<floatT>(attr::AttrOwner::POINT, "height");
    for (Offset point = 0; point < source.getNumPoints(); ++point)
        heights.setValue(point, static_cast<floatT>(point));

    geo::Mesh copy = source;
    copy.addPoint(Vector3(5, 5, 5));
    attr::AttributeHandleFloat copyHeights = copy.addAttribute<floatT>(attr::AttrOwner::POINT, "height");
    copyHeights.setValue(4, 40.f);
    copyHeights.setValue(0, 99.f);

    const auto sourceHeights =
        attr::AttributeHandleRO<floatT>(source.getAttribByName(attr::AttrOwner::POINT, "height"));
    REQUIRE(sourceHeights.getSize() == 4);
    REQUIRE(sourceHeights.getValue(0) == 0.f);
}

TEST_CASE("Writing an intrinsic attribute through addAttribute follows the mesh handles", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    geo::Mesh copy = source;

    auto positions = copy.tryAddAttribute<Vector3>(attr::AttrOwner::POINT, "Position", true);
    REQUIRE(positions);
    positions->setValue(0, Vector3(8, 8, 8));

    REQUIRE(copy.getPointPos(0) == Vector3(8, 8, 8));
    REQUIRE(source.getPointPos(0) == Vector3(0, 0, 0));
    REQUIRE(copy.intrinsicHandlesBound());
}

TEST_CASE("Assigning a mesh shares then detaches like a copy", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    geo::Mesh target;
    target = source;

    target.setPointPos(0, Vector3(2, 2, 2));
    target.addPoint(Vector3(3, 3, 3));

    REQUIRE(source.getPointPos(0) == Vector3(0, 0, 0));
    REQUIRE(source.getNumPoints() == 4);
    REQUIRE(target.getNumPoints() == 5);
    REQUIRE(target.intrinsicHandlesBound());
}

TEST_CASE("A copy of a copy detaches independently", "[copyOnWrite]")
{
    geo::Mesh first = makeGrid(1, 1);
    geo::Mesh second = first;
    geo::Mesh third = second;

    second.setPointPos(0, Vector3(1, 1, 1));
    third.setPointPos(0, Vector3(2, 2, 2));

    REQUIRE(first.getPointPos(0) == Vector3(0, 0, 0));
    REQUIRE(second.getPointPos(0) == Vector3(1, 1, 1));
    REQUIRE(third.getPointPos(0) == Vector3(2, 2, 2));
}

TEST_CASE("Merging a clone into its source leaves the clone intact", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(2, 1);
    const std::vector<Vector3> before = positionsOf(source);
    const Offset faces = source.getNumFaces();
    const Offset verts = source.getNumVerts();

    const auto clone = std::static_pointer_cast<geo::Mesh>(source.clone());
    source.merge(*clone);

    REQUIRE(clone->getNumPoints() == before.size());
    REQUIRE(clone->getNumFaces() == faces);
    REQUIRE(clone->getNumVerts() == verts);
    requireSameVectors(positionsOf(*clone), before);
    REQUIRE(vertexCountsOf(*clone).size() == faces);

    REQUIRE(source.getNumPoints() == before.size() * 2);
    REQUIRE(source.getNumFaces() == faces * 2);
    REQUIRE(source.getNumVerts() == verts * 2);
    for (size_t i = 0; i < before.size(); ++i)
    {
        REQUIRE(source.getPointPos(i) == before[i]);
        REQUIRE(source.getPointPos(before.size() + i) == before[i]);
    }
    // The merged faces point at the merged points.
    REQUIRE(source.getVertexPoint(verts) == static_cast<Offset>(before.size()));
    REQUIRE(source.intrinsicHandlesBound());
}

TEST_CASE("Merging several clones into their source gives each its own points", "[copyOnWrite]")
{
    geo::Mesh source = makeGrid(1, 1);
    std::vector<std::shared_ptr<geo::Mesh>> clones;
    for (int i = 1; i <= 3; ++i)
    {
        auto clone = std::static_pointer_cast<geo::Mesh>(source.clone());
        Matrix4 translation = Matrix4::Identity();
        translation(0, 3) = 10.f * i;
        clone->applyTransform(translation, TransformClass::POINT);
        clones.push_back(clone);
    }
    for (const auto& clone : clones)
        source.merge(*clone);

    REQUIRE(source.getNumPoints() == 16);
    REQUIRE(source.getPointPos(0) == Vector3(0, 0, 0));
    REQUIRE(source.getPointPos(4) == Vector3(10, 0, 0));
    REQUIRE(source.getPointPos(8) == Vector3(20, 0, 0));
    REQUIRE(source.getPointPos(12) == Vector3(30, 0, 0));
    REQUIRE(clones[0]->getNumPoints() == 4);
}

TEST_CASE("A copied camera keeps its own transform", "[copyOnWrite]")
{
    geo::Camera source;
    Matrix4 moved = Matrix4::Identity();
    moved(0, 3) = 4.f;
    source.setTransform(moved);

    geo::Camera copy = source;
    Matrix4 copyMoved = Matrix4::Identity();
    copyMoved(1, 3) = 5.f;
    copy.setTransform(copyMoved);

    REQUIRE(source.getTransform() == moved);
    REQUIRE(copy.getTransform() == copyMoved);

    // The source writes after the copy was taken.
    geo::Camera another = source;
    source.setTransform(Matrix4::Identity());
    REQUIRE(another.getTransform() == moved);
    REQUIRE(source.getTransform() == Matrix4::Identity());

    // Applying a transform goes through the same path.
    geo::Camera applied = another;
    applied.applyTransform(moved, TransformClass::PRIMITIVE);
    REQUIRE(another.getTransform() == moved);
    REQUIRE(applied.getTransform() == moved * moved);
}

// Threads

TEST_CASE("Threads copy and transform one published packet without disturbing it", "[copyOnWrite][threads]")
{
    constexpr size_t threadCount = 8;

    auto published = std::make_shared<NodePacket>();
    published->addPrimitive(std::make_shared<geo::Mesh>(makeGrid(40, 40)));
    const auto publishedMesh = std::static_pointer_cast<const geo::Mesh>(published->getPrimitive(0));
    const std::vector<Vector3> before = positionsOf(*publishedMesh);
    const Offset faces = publishedMesh->getNumFaces();
    const std::shared_ptr<const NodePacket> shared = published;

    for (int iteration = 0; iteration < 10; ++iteration)
    {
        StartGate gate(threadCount);
        std::vector<std::vector<Vector3>> results(threadCount);
        std::vector<Offset> pointCounts(threadCount, 0);
        std::vector<Offset> faceCounts(threadCount, 0);
        std::vector<bool> bound(threadCount, false);
        std::vector<std::thread> threads;
        for (size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&, t] {
                gate.wait();
                NodePacket copy = shared->deepCopy();
                const auto mesh = std::static_pointer_cast<geo::Mesh>(copy.getPrimitive(0));
                Matrix4 translation = Matrix4::Identity();
                translation(0, 3) = static_cast<floatT>(t + 1);
                mesh->applyTransform(translation, TransformClass::POINT);
                if (t % 2 == 0) mesh->addPoint(Vector3(0, 0, 0));
                if (t % 3 == 0) mesh->addFace({0, 1, 2});
                results[t] = positionsOf(*mesh);
                pointCounts[t] = mesh->getNumPoints();
                faceCounts[t] = mesh->getNumFaces();
                bound[t] = mesh->intrinsicHandlesBound();
            });
        }
        for (std::thread& thread : threads)
            thread.join();

        for (size_t t = 0; t < threadCount; ++t)
        {
            REQUIRE(bound[t]);
            REQUIRE(pointCounts[t] == before.size() + (t % 2 == 0 ? 1 : 0));
            REQUIRE(faceCounts[t] == faces + (t % 3 == 0 ? 1 : 0));
            for (size_t i = 0; i < before.size(); ++i)
                REQUIRE(results[t][i] == before[i] + Vector3(static_cast<floatT>(t + 1), 0, 0));
        }
        requireSameVectors(positionsOf(*publishedMesh), before);
        REQUIRE(publishedMesh->getNumFaces() == faces);
    }
}

TEST_CASE("Threads run a point script on copies of one input without disturbing it", "[copyOnWrite][threads]")
{
    constexpr size_t threadCount = 8;

    NodePacket input;
    input.addPrimitive(std::make_shared<geo::Mesh>(makeGrid(20, 20)));
    const auto inputMesh = std::static_pointer_cast<const geo::Mesh>(input.getPrimitive(0));
    const std::vector<Vector3> before = positionsOf(*inputMesh);

    std::vector<std::shared_ptr<script::PointScript>> scripts;
    for (size_t t = 0; t < threadCount; ++t)
    {
        String error;
        const String code =
            "@height = " + std::to_string(t + 1) + ".0\n@Position.y = " + std::to_string(t + 1) + ".0";
        auto compiled = script::PointScript::compile(code, error);
        INFO(error);
        REQUIRE(compiled);
        scripts.push_back(compiled);
    }

    const script::ScriptContext context(0, input);
    StartGate gate(threadCount);
    std::vector<std::shared_ptr<geo::Mesh>> outputs(threadCount);
    std::vector<bool> succeeded(threadCount, false);
    std::vector<std::thread> threads;
    for (size_t t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&, t] {
            gate.wait();
            auto output = std::make_shared<geo::Mesh>(*inputMesh);
            String error;
            const auto script = scripts[t]->clone();
            if (!script || !script->addWrittenAttributes(*output, error)) return;
            succeeded[t] = script->run(context, 0, *output, 0, output->getNumPoints(), error);
            outputs[t] = output;
        });
    }
    for (std::thread& thread : threads)
        thread.join();

    for (size_t t = 0; t < threadCount; ++t)
    {
        REQUIRE(succeeded[t]);
        const auto& output = *outputs[t];
        REQUIRE(output.intrinsicHandlesBound());
        const auto heights = attr::AttributeHandleRO<floatT>(
            output.getAttribByName(attr::AttrOwner::POINT, "height")
        );
        for (Offset point = 0; point < output.getNumPoints(); ++point)
        {
            REQUIRE(heights.getValue(point) == Catch::Approx(static_cast<floatT>(t + 1)));
            REQUIRE(output.getPointPos(point).y() == Catch::Approx(static_cast<floatT>(t + 1)));
            REQUIRE(output.getPointPos(point).x() == before[point].x());
        }
    }
    requireSameVectors(positionsOf(*inputMesh), before);
    REQUIRE_FALSE(inputMesh->getAttribByName(attr::AttrOwner::POINT, "height"));
}

// Networks

namespace {

Vector3 firstPointOf(nt::Node& node, Offset pointOffset = 0)
{
    const auto mesh =
        std::dynamic_pointer_cast<const geo::Mesh>(node.getOutputPacket(0)->getPrimitive(0));
    REQUIRE(mesh != nullptr);
    return mesh->getPointPos(pointOffset);
}

nt::NodeId addTransform(nt::NodeId source, float x, float y, float z)
{
    auto& nm = nt::nm();
    const nt::NodeId id = nm.createNode("helix::transform");
    nm.connectNodes(source, 0, id, 0);
    const auto translate = nm.getNode(id).getParameter("translate").lock();
    translate->setFloat(x, 0);
    translate->setFloat(y, 1);
    translate->setFloat(z, 2);
    return id;
}

} // namespace

TEST_CASE_METHOD(NMReset, "Sibling transforms and a chain leave their upstream outputs unchanged", "[copyOnWrite]")
{
    auto& nm = nt::nm();
    const nt::NodeId grid = nm.createNode("helix::grid");
    nm.getNode(grid).getParameter("rows").lock()->setInt(2);
    nm.getNode(grid).getParameter("columns").lock()->setInt(2);

    const nt::NodeId first = addTransform(grid, 1, 0, 0);
    const nt::NodeId sibling = addTransform(grid, 0, 5, 0);
    const nt::NodeId second = addTransform(first, 10, 0, 0);
    const nt::NodeId third = addTransform(second, 0, 0, 100);

    nm.cook(grid);
    const Vector3 gridPoint = firstPointOf(nm.getNode(grid));
    const std::vector<Vector3> gridPoints = positionsOf(*std::dynamic_pointer_cast<const geo::Mesh>(
        nm.getNode(grid).getOutputPacket(0)->getPrimitive(0)
    ));

    nm.cook(first);
    nm.cook(sibling);
    nm.cook(third);

    REQUIRE(firstPointOf(nm.getNode(grid)) == gridPoint);
    requireSameVectors(
        positionsOf(*std::dynamic_pointer_cast<const geo::Mesh>(
            nm.getNode(grid).getOutputPacket(0)->getPrimitive(0)
        )),
        gridPoints
    );
    REQUIRE(firstPointOf(nm.getNode(first)) == gridPoint + Vector3(1, 0, 0));
    REQUIRE(firstPointOf(nm.getNode(sibling)) == gridPoint + Vector3(0, 5, 0));
    REQUIRE(firstPointOf(nm.getNode(second)) == gridPoint + Vector3(11, 0, 0));
    REQUIRE(firstPointOf(nm.getNode(third)) == gridPoint + Vector3(11, 0, 100));

    // Changing a parameter upstream recooks the chain without leaking into the earlier outputs.
    nm.getNode(first).getParameter("translate").lock()->setFloat(2.f, 0);
    nm.cook(third);
    REQUIRE(firstPointOf(nm.getNode(grid)) == gridPoint);
    REQUIRE(firstPointOf(nm.getNode(first)) == gridPoint + Vector3(2, 0, 0));
    REQUIRE(firstPointOf(nm.getNode(sibling)) == gridPoint + Vector3(0, 5, 0));
    REQUIRE(firstPointOf(nm.getNode(third)) == gridPoint + Vector3(12, 0, 100));
}
