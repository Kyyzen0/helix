// What the timeline and the spreadsheet of the native app stand on, through the facade: the
// playback range and rate, keyframes, the key written by a plain edit of an animated value, and
// the numbers a table of attributes uses to know what it holds is still current.

#include "Engine/Network/NetworkManager.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include "TestPaths.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace core = helix::core;
using Catch::Approx;

namespace {

struct Fixture
{
    Fixture()
    {
        REQUIRE(core::initialize({}).ok);
        REQUIRE(core::newScene().ok);
        helix::sim::KeyframeStore::instance().clear();
    }
    ~Fixture() { helix::sim::KeyframeStore::instance().clear(); }
};

core::NodeId makeNode(const std::string& type, const std::string& name = "")
{
    const core::CreateNodeResult result = core::createNode(type, name, 0.f, 0.f);
    REQUIRE(result.status.ok);
    return result.id;
}

core::ParameterInfo parameterOf(core::NodeId node, const std::string& name)
{
    for (const core::ParameterInfo& info : core::listParameters(node))
        if (info.name == name) return info;
    FAIL("no parameter " << name);
    return {};
}

core::KeyframeTrackInfo trackOf(core::NodeId node, const std::string& parameter, std::uint32_t component)
{
    for (const core::KeyframeTrackInfo& track : core::listKeyframes(node))
        if (track.parameter == parameter && track.component == component) return track;
    FAIL("no track " << parameter << " " << component);
    return {};
}

// Every point position of a node, as the table of the spreadsheet reads them.
std::vector<float> positionsOf(core::NodeId node)
{
    const core::AttributeValues values = core::readAttribute(node, core::AttributeOwner::Point, "Position", 0, 1'000'000);
    REQUIRE(values.status.ok);
    return values.floats;
}

} // namespace

TEST_CASE_METHOD(Fixture, "The playback range is set whole, and the frame is held inside it", "[timeline]")
{
    REQUIRE(core::setFrameRange(10, 50).ok);
    core::TimeInfo time = core::timeInfo();
    REQUIRE(time.startFrame == 10);
    REQUIRE(time.endFrame == 50);
    // The scene sat on frame 1, which the new range does not hold.
    REQUIRE(time.frame == 10.f);

    // A range that moves up goes through the old end, and still comes out as asked.
    REQUIRE(core::setFrameRange(100, 200).ok);
    time = core::timeInfo();
    REQUIRE(time.startFrame == 100);
    REQUIRE(time.endFrame == 200);
    REQUIRE(time.frame == 100.f);

    core::setFrame(150.f);
    REQUIRE(core::setFrameRange(1, 120).ok);
    REQUIRE(core::timeInfo().frame == 120.f);
}

TEST_CASE_METHOD(Fixture, "A range that ends before it starts, or is absurd, is refused and changes nothing", "[timeline]")
{
    REQUIRE(core::setFrameRange(5, 30).ok);
    for (const auto& [start, end] : std::vector<std::pair<int, int>>{{30, 30}, {40, 3}, {0, 2'000'000}, {-2'000'000, 4}})
    {
        const core::Status status = core::setFrameRange(start, end);
        REQUIRE_FALSE(status.ok);
        REQUIRE_FALSE(status.error.empty());
    }
    REQUIRE(core::timeInfo().startFrame == 5);
    REQUIRE(core::timeInfo().endFrame == 30);
}

TEST_CASE_METHOD(Fixture, "A range and a rate are part of the scene, and count as a change", "[timeline]")
{
    REQUIRE(core::saveScene(helix::test::tempPath("helix_timeline_range.kut")).ok);
    REQUIRE_FALSE(core::isSceneModified());
    REQUIRE(core::setFrameRange(3, 90).ok);
    REQUIRE(core::isSceneModified());

    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);
    REQUIRE(core::setFps(60.f).ok);
    REQUIRE(core::setFrameRange(1, 10).ok);
    REQUIRE(core::restoreScene(snapshot.data).ok);
    REQUIRE(core::timeInfo().startFrame == 3);
    REQUIRE(core::timeInfo().endFrame == 90);
    REQUIRE(core::timeInfo().fps == Approx(24.f));
}

TEST_CASE_METHOD(Fixture, "A frame rate outside what a second can hold is refused", "[timeline]")
{
    REQUIRE(core::setFps(30.f).ok);
    REQUIRE(core::timeInfo().fps == Approx(30.f));
    for (const float fps : {0.f, -24.f, 1000.1f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()})
    {
        REQUIRE_FALSE(core::setFps(fps).ok);
    }
    REQUIRE(core::timeInfo().fps == Approx(30.f));
    REQUIRE(core::setFps(1000.f).ok);
}

TEST_CASE_METHOD(Fixture, "A simulation changes with the frame rate even though the frame did not move", "[timeline]")
{
    const core::NodeId simulation = makeNode("helix::simulation");
    REQUIRE(core::setInt(simulation, "particle_count", 0, 4).ok);
    REQUIRE(core::setFrameRange(1, 240).ok);
    core::setFrame(20.f);

    const std::vector<float> at24 = positionsOf(simulation);
    REQUIRE_FALSE(at24.empty());
    // Nothing moved, so a second read is the first.
    REQUIRE(positionsOf(simulation) == at24);

    // Particles fall for twenty frames: a frame lasting a different time gives a different fall.
    REQUIRE(core::setFps(48.f).ok);
    const std::vector<float> at48 = positionsOf(simulation);
    REQUIRE(at48.size() == at24.size());
    REQUIRE(at48 != at24);

    REQUIRE(core::setFps(24.f).ok);
    REQUIRE(positionsOf(simulation) == at24);
}

TEST_CASE_METHOD(Fixture, "A key is set, listed, replaced and removed", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::listKeyframes(transform).empty());
    REQUIRE(core::animatedNodes().empty());

    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 24.f, 10.f, core::Interpolation::Smooth).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 2, 12.f, 3.f, core::Interpolation::Step).ok);

    const std::vector<core::KeyframeTrackInfo> tracks = core::listKeyframes(transform);
    REQUIRE(tracks.size() == 2);
    REQUIRE(tracks[0].parameter == "translate");
    REQUIRE(tracks[0].component == 0);
    REQUIRE(tracks[0].keys.size() == 2);
    REQUIRE(tracks[0].keys[0].frame == 1.f);
    REQUIRE(tracks[0].keys[1].value == 10.f);
    REQUIRE(tracks[0].keys[1].interpolation == core::Interpolation::Smooth);
    REQUIRE(tracks[1].component == 2);
    REQUIRE(core::animatedNodes() == std::vector<core::NodeId>{transform});

    // The same frame again replaces the key.
    REQUIRE(core::setKeyframe(transform, "translate", 0, 24.f, 20.f, core::Interpolation::Linear).ok);
    REQUIRE(trackOf(transform, "translate", 0).keys.size() == 2);
    REQUIRE(trackOf(transform, "translate", 0).keys[1].value == 20.f);

    REQUIRE(core::removeKeyframe(transform, "translate", 0, 24.f).ok);
    REQUIRE(trackOf(transform, "translate", 0).keys.size() == 1);
    REQUIRE_FALSE(core::removeKeyframe(transform, "translate", 0, 24.f).ok);
}

TEST_CASE_METHOD(Fixture, "A parameter shows the value of its track at the current frame", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setKeyframe(transform, "translate", 1, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 1, 11.f, 10.f, core::Interpolation::Linear).ok);

    core::setFrame(6.f);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(5.f));
    core::setFrame(11.f);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(10.f));

    // A key made away from the frame the scene sits on still leaves the value of that frame.
    REQUIRE(core::setKeyframe(transform, "translate", 1, 21.f, 100.f, core::Interpolation::Linear).ok);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(10.f));
}

TEST_CASE_METHOD(Fixture, "Removing the last key puts back the value the parameter held", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setFloat(transform, "translate", 0, 7.f).ok);

    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 10.f, 9.f, core::Interpolation::Linear).ok);
    core::setFrame(10.f);
    REQUIRE(core::getFloat(transform, "translate", 0).value == Approx(9.f));
    REQUIRE(trackOf(transform, "translate", 0).baseValue == Approx(7.f));

    REQUIRE(core::removeKeyframe(transform, "translate", 0, 10.f).ok);
    // One key left, so the track still drives the value.
    REQUIRE(parameterOf(transform, "translate").animatedComponents == std::vector<std::uint32_t>{0});
    REQUIRE(core::removeKeyframe(transform, "translate", 0, 1.f).ok);

    REQUIRE(parameterOf(transform, "translate").animatedComponents.empty());
    REQUIRE(core::getFloat(transform, "translate", 0).value == Approx(7.f));
    core::setFrame(1.f);
    REQUIRE(core::getFloat(transform, "translate", 0).value == Approx(7.f));
    REQUIRE(core::animatedNodes().empty());
}

TEST_CASE_METHOD(Fixture, "A key on a parameter with an expression gives the expression back when cleared", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "translate", 1, "$F * 2").ok);
    REQUIRE(parameterOf(transform, "translate").expressions[1] == "$F * 2");

    REQUIRE(core::setKeyframe(transform, "translate", 1, 1.f, 5.f, core::Interpolation::Linear).ok);
    // The keys drive the component, and an expression cannot be put on top of them.
    REQUIRE_FALSE(core::setExpression(transform, "translate", 1, "1").ok);

    REQUIRE(core::clearKeyframes(transform, "translate", 1).ok);
    REQUIRE(parameterOf(transform, "translate").expressions[1] == "$F * 2");
    REQUIRE(parameterOf(transform, "translate").animatedComponents.empty());
    // Clearing a component that holds nothing is not an error.
    REQUIRE(core::clearKeyframes(transform, "translate", 1).ok);
}

TEST_CASE_METHOD(Fixture, "Set Keyframe keys the value the parameter has now", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setFloat(transform, "translate", 0, 3.5f).ok);
    REQUIRE(core::setKeyframeAtCurrentValue(transform, "translate", 0, 1.f).ok);
    REQUIRE(trackOf(transform, "translate", 0).keys[0].value == Approx(3.5f));
    REQUIRE(trackOf(transform, "translate", 0).keys[0].interpolation == core::Interpolation::Smooth);

    // A new key takes the interpolation of the one before it, and an existing one keeps its own.
    REQUIRE(core::setKeyframeInterpolation(transform, "translate", 0, 1.f, core::Interpolation::Step).ok);
    core::setFrame(8.f);
    REQUIRE(core::setKeyframeAtCurrentValue(transform, "translate", 0, 8.f).ok);
    REQUIRE(trackOf(transform, "translate", 0).keys[1].interpolation == core::Interpolation::Step);
    REQUIRE(core::setKeyframeInterpolation(transform, "translate", 0, 8.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframeAtCurrentValue(transform, "translate", 0, 8.f).ok);
    REQUIRE(trackOf(transform, "translate", 0).keys[1].interpolation == core::Interpolation::Linear);

    // The parameter says which of its components have a key on the frame the scene sits on.
    REQUIRE(parameterOf(transform, "translate").keyedComponents == std::vector<std::uint32_t>{0});
    core::setFrame(5.f);
    REQUIRE(parameterOf(transform, "translate").keyedComponents.empty());
    REQUIRE(parameterOf(transform, "translate").animatedComponents == std::vector<std::uint32_t>{0});
}

TEST_CASE_METHOD(Fixture, "A key moves to a free frame and is refused on an occupied one", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 0.f, core::Interpolation::Step).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 10.f, 5.f, core::Interpolation::Linear).ok);

    REQUIRE(core::moveKeyframe(transform, "translate", 0, 10.f, 14.f).ok);
    core::KeyframeTrackInfo track = trackOf(transform, "translate", 0);
    REQUIRE(track.keys.size() == 2);
    REQUIRE(track.keys[1].frame == 14.f);
    REQUIRE(track.keys[1].value == 5.f);
    REQUIRE(track.keys[1].interpolation == core::Interpolation::Linear);

    const core::Status occupied = core::moveKeyframe(transform, "translate", 0, 14.f, 1.f);
    REQUIRE_FALSE(occupied.ok);
    track = trackOf(transform, "translate", 0);
    REQUIRE(track.keys[0].value == 0.f);
    REQUIRE(track.keys[1].frame == 14.f);

    REQUIRE_FALSE(core::moveKeyframe(transform, "translate", 0, 3.f, 4.f).ok);
    REQUIRE(core::moveKeyframe(transform, "translate", 0, 14.f, 14.f).ok);

    // The only key of a track moves without the track forgetting the value it held.
    const core::NodeId other = makeNode("helix::transform");
    REQUIRE(core::setFloat(other, "translate", 0, 2.f).ok);
    REQUIRE(core::setKeyframe(other, "translate", 0, 5.f, 9.f, core::Interpolation::Linear).ok);
    REQUIRE(core::moveKeyframe(other, "translate", 0, 5.f, 7.f).ok);
    REQUIRE(trackOf(other, "translate", 0).baseValue == Approx(2.f));
    REQUIRE(core::clearKeyframes(other, "translate", 0).ok);
    REQUIRE(core::getFloat(other, "translate", 0).value == Approx(2.f));
}

TEST_CASE_METHOD(Fixture, "Editing a keyed component keys the current frame", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setKeyframe(transform, "translate", 1, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 1, 11.f, 10.f, core::Interpolation::Linear).ok);

    // On a frame with no key, the edit makes one, and the keys on the other frames stay.
    core::setFrame(6.f);
    REQUIRE(core::setFloat(transform, "translate", 1, 20.f).ok);
    core::KeyframeTrackInfo track = trackOf(transform, "translate", 1);
    REQUIRE(track.keys.size() == 3);
    REQUIRE(track.keys[1].frame == 6.f);
    REQUIRE(track.keys[1].value == 20.f);
    REQUIRE(track.keys[1].interpolation == core::Interpolation::Linear);
    REQUIRE(track.keys[0].value == 0.f);
    REQUIRE(track.keys[2].value == 10.f);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(20.f));

    // On a frame with a key, the edit replaces its value.
    REQUIRE(core::setFloat(transform, "translate", 1, 30.f).ok);
    track = trackOf(transform, "translate", 1);
    REQUIRE(track.keys.size() == 3);
    REQUIRE(track.keys[1].value == 30.f);

    // The value stays where the edit put it when the scene leaves the frame and comes back.
    core::setFrame(11.f);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(10.f));
    core::setFrame(6.f);
    REQUIRE(core::getFloat(transform, "translate", 1).value == Approx(30.f));

    // A vector write keys the components that are animated and writes the others as it always did.
    REQUIRE(core::setVector3(transform, "translate", 4.f, 40.f, 6.f).ok);
    REQUIRE(trackOf(transform, "translate", 1).keys[1].value == 40.f);
    REQUIRE(core::getFloat(transform, "translate", 0).value == Approx(4.f));
    REQUIRE(core::getFloat(transform, "translate", 2).value == Approx(6.f));
    REQUIRE(core::listKeyframes(transform).size() == 1);
}

TEST_CASE_METHOD(Fixture, "A value keyed past a locked end of its range is held to it", "[keyframes]")
{
    // The columns of a torus are an integer, the radii are floats with a locked minimum.
    const core::NodeId torus = makeNode("helix::torus");
    const core::ParameterInfo radius = parameterOf(torus, "radius");
    REQUIRE(radius.hasRange);
    if (!radius.minLocked) SKIP("the radius of a torus has no locked minimum");
    REQUIRE(core::setKeyframe(torus, "radius", 0, 1.f, radius.rangeMin - 5.f, core::Interpolation::Linear).ok);
    REQUIRE(trackOf(torus, "radius", 0).keys[0].value == Approx(radius.rangeMin));
}

TEST_CASE_METHOD(Fixture, "A key that cannot be made says why", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    const float nan = std::numeric_limits<float>::quiet_NaN();

    REQUIRE_FALSE(core::setKeyframe(transform, "selection", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "nothing", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "translate", 3, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "translate", 0, nan, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "translate", 0, 5e6f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "translate", 0, 1.f, nan, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframe(9999, "translate", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE_FALSE(core::setKeyframeAtCurrentValue(transform, "selection", 0, 1.f).ok);
    REQUIRE_FALSE(core::clearKeyframes(transform, "selection", 0).ok);
    REQUIRE_FALSE(core::setKeyframe(transform, "selection", 0, 1.f, 1.f, core::Interpolation::Linear).error.empty());
    REQUIRE(core::listKeyframes(transform).empty());
    REQUIRE(core::listKeyframes(9999).empty());

    // An integer is not animated, and the message says so.
    const core::NodeId grid = makeNode("helix::grid");
    const core::Status integer = core::setKeyframe(grid, "rows", 0, 1.f, 3.f, core::Interpolation::Linear);
    REQUIRE_FALSE(integer.ok);
    REQUIRE(integer.error.find("float") != std::string::npos);
}

TEST_CASE_METHOD(Fixture, "A reset of a keyed parameter says to clear the keys first", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    const core::Status reset = core::resetParameter(transform, "translate");
    REQUIRE_FALSE(reset.ok);
    REQUIRE(reset.error.find("Clear its keyframes") != std::string::npos);
    REQUIRE(core::clearKeyframes(transform, "translate", 0).ok);
    REQUIRE(core::resetParameter(transform, "translate").ok);
}

TEST_CASE_METHOD(Fixture, "Keys travel with the scene through a capture", "[keyframes]")
{
    const core::NodeId transform = makeNode("helix::transform", "mover");
    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 0.f, core::Interpolation::Smooth).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 24.f, 8.f, core::Interpolation::Step).ok);
    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);

    REQUIRE(core::clearKeyframes(transform, "translate", 0).ok);
    REQUIRE(core::restoreScene(snapshot.data).ok);

    core::NodeId restored = 0;
    for (const core::NodeInfo& info : core::listNodes())
        if (info.name == "mover") restored = info.id;
    REQUIRE(restored != 0);
    const core::KeyframeTrackInfo track = trackOf(restored, "translate", 0);
    REQUIRE(track.keys.size() == 2);
    REQUIRE(track.keys[0].interpolation == core::Interpolation::Smooth);
    REQUIRE(track.keys[1].interpolation == core::Interpolation::Step);
}

TEST_CASE_METHOD(Fixture, "A keyed transform gives the same geometry whatever order the frames were visited in", "[keyframes][purity]")
{
    const core::NodeId cube = makeNode("helix::cube");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::connectNodes(cube, 0, transform, 0).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 0.f, core::Interpolation::Smooth).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 24.f, 10.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "rotate", 1, 12.f, 90.f, core::Interpolation::Step).ok);
    REQUIRE(core::setDisplayNode(transform).ok);

    // Playing forward, frame by frame.
    std::vector<std::vector<float>> forward;
    for (int frame = 1; frame <= 24; ++frame)
    {
        core::setFrame(static_cast<float>(frame));
        forward.push_back(positionsOf(transform));
    }
    REQUIRE(forward.front() != forward.back());

    // Jumping around, then looking at the same frames.
    for (const int frame : {24, 3, 17, 1, 12, 13, 2, 24, 9})
    {
        core::setFrame(static_cast<float>(frame));
        const std::vector<float> positions = positionsOf(transform);
        REQUIRE(positions.size() == forward[static_cast<std::size_t>(frame - 1)].size());
        for (std::size_t index = 0; index < positions.size(); ++index)
            REQUIRE(positions[index] == Approx(forward[static_cast<std::size_t>(frame - 1)][index]).margin(1e-5));
    }
}

TEST_CASE_METHOD(Fixture, "A simulation gives the same points at a frame whatever order the frames were visited in", "[keyframes][purity]")
{
    const core::NodeId simulation = makeNode("helix::simulation");
    REQUIRE(core::setInt(simulation, "particle_count", 0, 16).ok);
    // The pull is keyed, so what the simulation reads changes with the frames before the current one.
    REQUIRE(core::setKeyframe(simulation, "particle_gravity", 1, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(simulation, "particle_gravity", 1, 30.f, -20.f, core::Interpolation::Linear).ok);

    std::vector<std::vector<float>> forward;
    for (int frame = 1; frame <= 30; ++frame)
    {
        core::setFrame(static_cast<float>(frame));
        forward.push_back(positionsOf(simulation));
    }
    REQUIRE(forward.front() != forward.back());

    for (const int frame : {30, 4, 21, 1, 10, 29})
    {
        core::setFrame(static_cast<float>(frame));
        const std::vector<float> positions = positionsOf(simulation);
        const std::vector<float>& expected = forward[static_cast<std::size_t>(frame - 1)];
        REQUIRE(positions.size() == expected.size());
        for (std::size_t index = 0; index < positions.size(); ++index)
            REQUIRE(positions[index] == Approx(expected[index]).margin(1e-5));
    }

    // A key moved on another frame changes the frames after it, even where the value of the
    // parameter at the frame the scene sits on did not.
    core::setFrame(30.f);
    const std::vector<float> before = positionsOf(simulation);
    REQUIRE(core::moveKeyframe(simulation, "particle_gravity", 1, 1.f, 15.f).ok);
    REQUIRE(core::getFloat(simulation, "particle_gravity", 1).value == Approx(-20.f));
    REQUIRE(positionsOf(simulation) != before);
}

TEST_CASE_METHOD(Fixture, "The cook generation changes with a cook and not with a read", "[spreadsheet]")
{
    const core::NodeId cube = makeNode("helix::cube");
    REQUIRE(core::nodeCookGeneration(cube) == 0);
    REQUIRE(core::nodeCookGeneration(9999) == 0);

    const core::GeometryReport report = core::cookNodeReport(cube);
    REQUIRE(report.status.ok);
    REQUIRE(report.generation != 0);
    REQUIRE(core::nodeCookGeneration(cube) == report.generation);

    // The node is clean, so reading its values cooks nothing and says the same number.
    const core::AttributeValues values = core::readAttribute(cube, core::AttributeOwner::Point, "Position", 0, 4);
    REQUIRE(values.status.ok);
    REQUIRE(values.generation == report.generation);
    REQUIRE(core::readAttribute(cube, core::AttributeOwner::Point, "Position", 4, 4).generation == report.generation);
    REQUIRE(core::cookNodeReport(cube).generation == report.generation);

    // A change cooks the node again the next time something asks, and the number moves.
    REQUIRE(core::setVector3(cube, "size", 3.f, 3.f, 3.f).ok);
    REQUIRE(core::nodeCookGeneration(cube) == report.generation);
    const core::AttributeValues after = core::readAttribute(cube, core::AttributeOwner::Point, "Position", 0, 4);
    REQUIRE(after.generation != report.generation);
    REQUIRE(after.floats != values.floats);
}

TEST_CASE_METHOD(Fixture, "A node the frame moves has a new generation at each frame", "[spreadsheet]")
{
    const core::NodeId simulation = makeNode("helix::simulation");
    core::setFrame(1.f);
    const std::uint64_t first = core::cookNodeReport(simulation).generation;
    core::setFrame(2.f);
    const std::uint64_t second = core::cookNodeReport(simulation).generation;
    REQUIRE(second != first);
    // Another node's cook never reuses a number either.
    const core::NodeId cube = makeNode("helix::cube");
    const std::uint64_t third = core::cookNodeReport(cube).generation;
    REQUIRE(third != first);
    REQUIRE(third != second);
}

TEST_CASE_METHOD(Fixture, "The report says where each primitive's elements start", "[spreadsheet]")
{
    const core::NodeId cube = makeNode("helix::cube");
    const core::GeometryReport single = core::cookNodeReport(cube);
    REQUIRE(single.primitives.size() == 1);
    REQUIRE(single.primitives[0].pointStart == 0);
    REQUIRE(single.primitives[0].pointCount == single.pointCount);
    REQUIRE(single.primitives[0].vertexCount == single.vertexCount);
    REQUIRE(single.primitives[0].faceCount == single.faceCount);

    // A file of two meshes, kept apart.
    const core::CreateNodeResult imported = core::importFile(std::string(HELIX_TEST_DATA_DIR) + "/import/hierarchy.glb");
    REQUIRE(imported.status.ok);
    REQUIRE(core::setInt(imported.id, "mergeMeshes", 0, 0).ok);
    const core::GeometryReport pair = core::cookNodeReport(imported.id);
    REQUIRE(pair.status.ok);
    REQUIRE(pair.primitiveCount == 2);
    REQUIRE(pair.primitives.size() == 2);
    REQUIRE(pair.primitives[0].pointStart == 0);
    REQUIRE(pair.primitives[1].pointStart == pair.primitives[0].pointCount);
    REQUIRE(pair.primitives[1].vertexStart == pair.primitives[0].vertexCount);
    REQUIRE(pair.primitives[1].faceStart == pair.primitives[0].faceCount);
    REQUIRE(pair.primitives[0].pointCount + pair.primitives[1].pointCount == pair.pointCount);
    REQUIRE(pair.primitives[0].faceCount + pair.primitives[1].faceCount == pair.faceCount);
}

// What one frame of playback costs on the thread that plays: moving the frame and cooking the
// display node, with the part of the viewport that does not need a window (the display buffers
// are what the viewport uploads). Hidden from the normal run, since a time is not a check:
//   build-core/helix.app/Contents/bin/tests "[.bench]"
TEST_CASE_METHOD(Fixture, "The cost of a frame of playback", "[.bench]")
{
    const auto costOf = [](core::NodeId display, float frame, int repeats) {
        REQUIRE(core::setDisplayNode(display).ok);
        // The first visit builds what is cached, and is not what playback pays at every frame.
        core::setFrame(frame - 1.f);
        REQUIRE(core::cookDisplayBuffers().status.ok);
        double total = 0.0;
        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            // Away and back, so that each timed step is a move to a frame that has to cook: the cook
            // cache is emptied, since a frame it holds costs a lookup (see CookCacheTests.cpp, which
            // measures that).
            core::setFrame(frame - 1.f);
            core::cookDisplayBuffers();
            core::clearCookCache();
            const auto start = std::chrono::steady_clock::now();
            core::setFrame(frame);
            const core::DisplayBuffers buffers = core::cookDisplayBuffers();
            total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            REQUIRE(buffers.status.ok);
        }
        return total / repeats;
    };

    REQUIRE(core::setFrameRange(1, 1000).ok);

    const std::string samples = std::string(HELIX_TEST_DATA_DIR) + "/../../apple/Helix/Samples/Waves.kut";
    REQUIRE(core::openScene(samples).ok);
    REQUIRE(core::setFrameRange(1, 1000).ok);
    WARN("Waves.kut, frame 24: " << costOf(core::displayNode(), 24.f, 20) << " ms");

    REQUIRE(core::newScene().ok);
    REQUIRE(core::setFrameRange(1, 1000).ok);
    const core::CreateNodeResult quad = core::importFile(std::string(HELIX_TEST_DATA_DIR) + "/import/textured-quad.glb");
    REQUIRE(quad.status.ok);
    const core::NodeId mover = makeNode("helix::transform");
    REQUIRE(core::connectNodes(quad.id, 0, mover, 0).ok);
    REQUIRE(core::setKeyframe(mover, "translate", 0, 1.f, 0.f, core::Interpolation::Smooth).ok);
    REQUIRE(core::setKeyframe(mover, "translate", 0, 240.f, 10.f, core::Interpolation::Linear).ok);
    WARN("keyed transform on the textured quad, frame 100: " << costOf(mover, 100.f, 50) << " ms");

    REQUIRE(core::newScene().ok);
    REQUIRE(core::setFrameRange(1, 1000).ok);
    const core::NodeId simulation = makeNode("helix::simulation");
    for (const float frame : {10.f, 100.f, 500.f})
        WARN("simulation (default settings), frame " << frame << ": " << costOf(simulation, frame, 5) << " ms");
    REQUIRE(core::setInt(simulation, "particle_count", 0, 20000).ok);
    for (const float frame : {10.f, 100.f, 500.f})
        WARN("simulation (20000 particles), frame " << frame << ": " << costOf(simulation, frame, 3) << " ms");
}

TEST_CASE_METHOD(Fixture, "A restore brings back the captured frame, and the playhead is put back after it", "[timeline]")
{
    const core::NodeId transform = makeNode("helix::transform", "mover");
    REQUIRE(core::setKeyframe(transform, "translate", 0, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 0, 21.f, 20.f, core::Interpolation::Linear).ok);
    core::setFrame(5.f);
    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);

    // The playhead moves on, and an undo restores the scene captured when it was on frame 5.
    core::setFrame(16.f);
    REQUIRE(core::restoreScene(snapshot.data).ok);
    // This is the pitfall: the capture holds the frame, so a restore moves the playhead.
    REQUIRE(core::timeInfo().frame == 5.f);

    // The app puts the playhead back by setting the frame it had, which also brings the keyed
    // values to that frame: the restore left them where the capture had them.
    core::setFrame(16.f);
    REQUIRE(core::timeInfo().frame == 16.f);
    core::NodeId restored = 0;
    for (const core::NodeInfo& info : core::listNodes())
        if (info.name == "mover") restored = info.id;
    REQUIRE(restored != 0);
    REQUIRE(core::getFloat(restored, "translate", 0).value == Approx(15.f));

    // A restore that changes the range still leaves the frame inside it.
    REQUIRE(core::setFrameRange(1, 10).ok);
    core::setFrame(10.f);
    REQUIRE(core::restoreScene(snapshot.data).ok);
    REQUIRE(core::timeInfo().endFrame == 240);
    core::setFrame(16.f);
    REQUIRE(core::timeInfo().frame == 16.f);
}
