#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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

} // namespace

TEST_CASE_METHOD(Fixture, "A hide condition follows the parameter it reads", "[inspector]")
{
    const core::NodeId noise = makeNode("helix::attributeNoise");

    // The cellular mode only means something for cellular noise.
    REQUIRE(core::getString(noise, "noiseType", 0).value == "simplex");
    REQUIRE(parameterOf(noise, "cellularMode").hidden);
    REQUIRE_FALSE(parameterOf(noise, "noiseType").hidden);

    REQUIRE(core::setString(noise, "noiseType", 0, "cellular").ok);
    REQUIRE_FALSE(parameterOf(noise, "cellularMode").hidden);

    // The octaves hide behind the fractal type, a dropdown read by its index.
    REQUIRE(parameterOf(noise, "octaves").hidden);
    REQUIRE(core::setString(noise, "fractalType", 0, "fbm").ok);
    REQUIRE_FALSE(parameterOf(noise, "octaves").hidden);
}

TEST_CASE_METHOD(Fixture, "A disable condition greys a parameter out without hiding it", "[inspector]")
{
    const core::NodeId sweep = makeNode("helix::sweep");
    const core::ParameterInfo ramp = parameterOf(sweep, "scaleRamp");
    REQUIRE(ramp.type == core::ParameterType::Ramp);
    REQUIRE_FALSE(ramp.enabled);
    REQUIRE_FALSE(ramp.hidden);

    REQUIRE(core::setInt(sweep, "applyScale", 0, 1).ok);
    REQUIRE(parameterOf(sweep, "scaleRamp").enabled);
}

TEST_CASE_METHOD(Fixture, "Ranges and their locks come from the node", "[inspector]")
{
    const core::NodeId torus = makeNode("helix::torus");

    // Locked at the bottom only: a slider stops at 10 but a value may go past it.
    const core::ParameterInfo radius = parameterOf(torus, "radius");
    REQUIRE(radius.hasRange);
    REQUIRE(radius.componentCount == 2);
    REQUIRE(radius.rangeMin == 0.f);
    REQUIRE(radius.rangeMax == 10.f);
    REQUIRE(radius.minLocked);
    REQUIRE_FALSE(radius.maxLocked);

    const core::ParameterInfo center = parameterOf(torus, "center");
    REQUIRE(center.rangeMin == -10.f);
    REQUIRE(center.rangeMax == 10.f);
    REQUIRE_FALSE(center.minLocked);
    REQUIRE(center.style == "xyz");

    const core::ParameterInfo columns = parameterOf(torus, "columns");
    REQUIRE(columns.type == core::ParameterType::Int);
    REQUIRE(columns.rangeMin == 3.f);
    REQUIRE(columns.rangeMax == 64.f);
    REQUIRE(columns.minLocked);

    // A locked end is a limit the engine enforces, so the panel and the engine agree.
    REQUIRE(core::setInt(torus, "columns", 0, 1).ok);
    REQUIRE(core::getInt(torus, "columns", 0).value == 3);
    REQUIRE(core::setFloat(torus, "radius", 0, 500.f).ok);
    REQUIRE(core::getFloat(torus, "radius", 0).value == Approx(500.f));

    // A dropdown has no range.
    REQUIRE_FALSE(parameterOf(torus, "axis").hasRange);
}

TEST_CASE_METHOD(Fixture, "A dropdown lists its options with their labels", "[inspector]")
{
    const core::NodeId torus = makeNode("helix::torus");
    const core::ParameterInfo axis = parameterOf(torus, "axis");
    REQUIRE(axis.type == core::ParameterType::Dropdown);
    REQUIRE(axis.options.size() == 3);
    REQUIRE(axis.options[0].value == "x");
    REQUIRE(axis.options[0].label == "X Axis");
    REQUIRE(axis.options[2].value == "z");
    REQUIRE(axis.defaultStrings == std::vector<std::string>{"y"});
    REQUIRE(parameterOf(torus, "columns").options.empty());
}

TEST_CASE_METHOD(Fixture, "A parameter says what it is: tooltip, style, defaults, group", "[inspector]")
{
    const core::NodeId noise = makeNode("helix::attributeNoise");
    const core::ParameterInfo name = parameterOf(noise, "name");
    REQUIRE(name.style == "attribute");
    REQUIRE_FALSE(name.tooltip.empty());
    REQUIRE(name.defaultStrings == std::vector<std::string>{"noise"});
    bool ownersFound = false;
    for (const core::ParameterStyleOption& option : name.styleOptions)
        if (option.name == "owners" && option.value == "point") ownersFound = true;
    REQUIRE(ownersFound);

    const core::ParameterInfo offset = parameterOf(noise, "offset");
    REQUIRE(offset.defaultFloats == std::vector<float>{0.f, 0.f, 0.f});
    REQUIRE(offset.group.empty());

    // A parameter inside a group names the group, and hides with it.
    const core::ParameterInfo along = parameterOf(noise, "alongVector");
    REQUIRE(along.group == "Noise Along Vector");
    REQUIRE(along.labelHidden);
    REQUIRE(along.hidden); // the group is hidden until the type is a vector
    REQUIRE(core::setString(noise, "type", 0, "vector").ok);
    REQUIRE_FALSE(parameterOf(noise, "alongVector").hidden);

    REQUIRE(core::listParameters(0).empty());
    REQUIRE(core::listParameterLayout(0).empty());
}

TEST_CASE_METHOD(Fixture, "The layout keeps the order of the node, with groups and dividers", "[inspector]")
{
    const core::NodeId noise = makeNode("helix::attributeNoise");
    const std::vector<core::ParameterLayoutItem> layout = core::listParameterLayout(noise);
    REQUIRE(layout.size() > 8);

    // The first entries, as node.yaml lists them: a divider, two parameters, then a group.
    REQUIRE(layout[0].kind == core::ParameterLayoutKind::Divider);
    REQUIRE(layout[0].label == "Attribute");
    REQUIRE(layout[0].icon == "tag");
    REQUIRE(layout[1].kind == core::ParameterLayoutKind::Parameter);
    REQUIRE(layout[1].name == "name");
    REQUIRE(layout[2].name == "type");
    REQUIRE(layout[3].kind == core::ParameterLayoutKind::GroupBegin);
    REQUIRE(layout[3].label == "Noise Along Vector");
    REQUIRE(layout[3].horizontal);
    REQUIRE(layout[3].labelInline);
    REQUIRE(layout[3].hidden);
    REQUIRE(layout[4].name == "alongVector");
    REQUIRE(layout[4].hidden); // inside a hidden group
    REQUIRE(layout[5].name == "alongVectorAttribute");
    REQUIRE(layout[6].kind == core::ParameterLayoutKind::GroupEnd);
    REQUIRE(layout[7].name == "operation");

    // Every group that opens closes, and every parameter of listParameters is placed once.
    int depth = 0;
    std::vector<std::string> placed;
    for (const core::ParameterLayoutItem& item : layout)
    {
        if (item.kind == core::ParameterLayoutKind::GroupBegin) ++depth;
        if (item.kind == core::ParameterLayoutKind::GroupEnd) --depth;
        REQUIRE(depth >= 0);
        if (item.kind == core::ParameterLayoutKind::Parameter) placed.push_back(item.name);
    }
    REQUIRE(depth == 0);
    for (const core::ParameterInfo& info : core::listParameters(noise))
    {
        if (info.type == core::ParameterType::Other) continue; // a divider
        REQUIRE(std::count(placed.begin(), placed.end(), info.name) == 1);
    }

    // The group shows once the condition that hides it no longer holds.
    REQUIRE(core::setString(noise, "type", 0, "vector").ok);
    REQUIRE_FALSE(core::listParameterLayout(noise)[3].hidden);
}

TEST_CASE_METHOD(Fixture, "A disabled group disables what is inside it", "[inspector]")
{
    // The cylinder puts its cap options in groups; hiding or disabling one has to reach its members.
    const core::NodeId extrude = makeNode("helix::extrude");
    bool sawGroup = false;
    for (const core::ParameterLayoutItem& item : core::listParameterLayout(extrude))
        if (item.kind == core::ParameterLayoutKind::GroupBegin) sawGroup = true;
    REQUIRE(sawGroup);
}

TEST_CASE_METHOD(Fixture, "Defaults come back with a reset, which also drops the expressions", "[inspector]")
{
    const core::NodeId torus = makeNode("helix::torus");
    REQUIRE(core::setFloat(torus, "radius", 0, 4.f).ok);
    REQUIRE(core::setFloat(torus, "radius", 1, 2.f).ok);
    REQUIRE(core::setExpression(torus, "radius", 1, "1.5 + 1.5").ok);
    REQUIRE(core::getFloat(torus, "radius", 1).value == Approx(3.f));

    const core::ParameterInfo radius = parameterOf(torus, "radius");
    REQUIRE(radius.defaultFloats == std::vector<float>{1.f, 0.5f});

    REQUIRE(core::resetParameter(torus, "radius").ok);
    REQUIRE(core::getFloat(torus, "radius", 0).value == Approx(1.f));
    REQUIRE(core::getFloat(torus, "radius", 1).value == Approx(0.5f));
    REQUIRE(parameterOf(torus, "radius").expressions == std::vector<std::string>{"", ""});

    // A dropdown and an integer go back too.
    REQUIRE(core::setString(torus, "axis", 0, "z").ok);
    REQUIRE(core::setInt(torus, "columns", 0, 40).ok);
    REQUIRE(core::resetParameter(torus, "axis").ok);
    REQUIRE(core::resetParameter(torus, "columns").ok);
    REQUIRE(core::getString(torus, "axis", 0).value == "y");
    REQUIRE(core::getInt(torus, "columns", 0).value == 24);

    REQUIRE_FALSE(core::resetParameter(torus, "nothing").ok);
    REQUIRE_FALSE(core::resetParameter(9999, "radius").ok);
}

TEST_CASE_METHOD(Fixture, "An expression is stored, evaluated, reported when broken, and cleared", "[inspector]")
{
    const core::NodeId torus = makeNode("helix::torus");
    REQUIRE(core::setFloat(torus, "uniformScale", 0, 2.f).ok);

    REQUIRE(core::setExpression(torus, "uniformScale", 0, "2 + 3").ok);
    core::ParameterInfo info = parameterOf(torus, "uniformScale");
    REQUIRE(info.expressions == std::vector<std::string>{"2 + 3"});
    REQUIRE(info.expressionErrors == std::vector<std::string>{""});
    REQUIRE(core::getFloat(torus, "uniformScale", 0).value == Approx(5.f));

    // A broken expression is kept, as the person is still typing it, and says what is wrong.
    REQUIRE(core::setExpression(torus, "uniformScale", 0, "2 +").ok);
    info = parameterOf(torus, "uniformScale");
    REQUIRE(info.expressions == std::vector<std::string>{"2 +"});
    REQUIRE_FALSE(info.expressionErrors[0].empty());

    // Clearing gives the literal back, which an expression never overwrote.
    REQUIRE(core::clearExpression(torus, "uniformScale", 0).ok);
    info = parameterOf(torus, "uniformScale");
    REQUIRE(info.expressions == std::vector<std::string>{""});
    REQUIRE(info.expressionErrors == std::vector<std::string>{""});
    REQUIRE(core::getFloat(torus, "uniformScale", 0).value == Approx(2.f));
    REQUIRE(core::clearExpression(torus, "uniformScale", 0).ok); // nothing to clear is fine

    // Writing a value takes an expression off, as before.
    REQUIRE(core::setExpression(torus, "uniformScale", 0, "7").ok);
    REQUIRE(core::setFloat(torus, "uniformScale", 0, 1.f).ok);
    REQUIRE(parameterOf(torus, "uniformScale").expressions == std::vector<std::string>{""});
}

TEST_CASE_METHOD(Fixture, "An expression that cannot be set is refused with a reason", "[inspector]")
{
    const core::NodeId sweep = makeNode("helix::sweep");
    const core::NodeId torus = makeNode("helix::torus");

    REQUIRE_FALSE(core::setExpression(torus, "radius", 0, "").ok);
    REQUIRE_FALSE(core::setExpression(torus, "radius", 0, "   \n").ok);
    REQUIRE_FALSE(core::setExpression(torus, "radius", 5, "1").ok);
    REQUIRE_FALSE(core::setExpression(torus, "nothing", 0, "1").ok);
    REQUIRE_FALSE(core::setExpression(torus, "axis", 0, "1").ok);       // a dropdown takes an option
    REQUIRE_FALSE(core::setExpression(sweep, "scaleRamp", 0, "1").ok);  // a ramp has points
    REQUIRE_FALSE(core::setExpression(9999, "radius", 0, "1").ok);
    const core::Status tooLong = core::setExpression(torus, "radius", 0, std::string(5000, '1'));
    REQUIRE_FALSE(tooLong.ok);
    REQUIRE(tooLong.error.size() < 200); // the message does not repeat the input
    REQUIRE(parameterOf(torus, "radius").expressions == std::vector<std::string>{"", ""});
}

TEST_CASE_METHOD(Fixture, "A parameter with keyframes shows as animated and refuses what the timeline would undo", "[inspector]")
{
    const core::NodeId torus = makeNode("helix::torus");
    helix::sim::KeyframeStore::instance().setKeyframe({torus, "radius", 1}, 1.f, 0.2f);
    helix::sim::KeyframeStore::instance().setKeyframe({torus, "radius", 1}, 10.f, 0.8f);

    const core::ParameterInfo radius = parameterOf(torus, "radius");
    REQUIRE(radius.animatedComponents == std::vector<std::uint32_t>{1});
    REQUIRE(parameterOf(torus, "columns").animatedComponents.empty());

    REQUIRE_FALSE(core::setExpression(torus, "radius", 1, "1").ok);
    REQUIRE_FALSE(core::resetParameter(torus, "radius").ok);
    // The component next to it is not animated.
    REQUIRE(core::setExpression(torus, "radius", 0, "1").ok);
}

TEST_CASE_METHOD(Fixture, "A ramp is read and replaced as a list of points", "[inspector]")
{
    const core::NodeId sweep = makeNode("helix::sweep");
    core::RampInfo ramp = core::getRamp(sweep, "scaleRamp");
    REQUIRE(ramp.status.ok);
    REQUIRE(ramp.points.size() == 2);
    REQUIRE(ramp.points[0].position == Approx(0.f));
    REQUIRE(ramp.points[1].position == Approx(1.f));
    REQUIRE(ramp.points[0].interpolation == "linear");
    REQUIRE(ramp.interpolations.size() == 3);
    REQUIRE(ramp.interpolations[2].value == "bspline");

    const std::vector<core::RampPoint> wanted = {
        {0.f, 1.f, "linear"}, {0.4f, 2.f, "bspline"}, {1.f, 0.5f, "constant"}
    };
    REQUIRE(core::setRamp(sweep, "scaleRamp", wanted).ok);
    ramp = core::getRamp(sweep, "scaleRamp");
    REQUIRE(ramp.points.size() == 3);
    REQUIRE(ramp.points[1].position == Approx(0.4f));
    REQUIRE(ramp.points[1].value == Approx(2.f));
    REQUIRE(ramp.points[1].interpolation == "bspline");
    REQUIRE(ramp.points[2].interpolation == "constant");

    // It survives the capture an undo step is made of.
    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);
    REQUIRE(core::setRamp(sweep, "scaleRamp", {{0.f, 0.f, "linear"}, {1.f, 1.f, "linear"}}).ok);
    REQUIRE(core::restoreScene(snapshot.data).ok);
    core::NodeId restored = 0;
    for (const core::NodeInfo& info : core::listNodes())
        if (info.path == "/sweep1") restored = info.id;
    REQUIRE(restored != 0);
    REQUIRE(core::getRamp(restored, "scaleRamp").points.size() == 3);

    // Reset gives the default points back.
    REQUIRE(core::resetParameter(restored, "scaleRamp").ok);
    REQUIRE(core::getRamp(restored, "scaleRamp").points.size() == 2);
}

TEST_CASE_METHOD(Fixture, "A ramp that cannot be read by the sampler is refused and left as it was", "[inspector]")
{
    const core::NodeId sweep = makeNode("helix::sweep");
    const core::NodeId torus = makeNode("helix::torus");
    REQUIRE_FALSE(core::setRamp(sweep, "scaleRamp", {}).ok);
    REQUIRE_FALSE(core::setRamp(sweep, "scaleRamp", {{0.f, 1.f, "linear"}}).ok);
    REQUIRE_FALSE(core::setRamp(sweep, "scaleRamp", {{0.f, 1.f, "linear"}, {1.f, 1.f, "wobbly"}}).ok);
    const float nan = std::nanf("");
    REQUIRE_FALSE(core::setRamp(sweep, "scaleRamp", {{0.f, 1.f, "linear"}, {nan, 1.f, "linear"}}).ok);
    REQUIRE_FALSE(core::setRamp(sweep, "scaleRamp", std::vector<core::RampPoint>(65, {0.f, 1.f, "linear"})).ok);
    REQUIRE_FALSE(core::setRamp(torus, "radius", {{0.f, 1.f, "linear"}, {1.f, 1.f, "linear"}}).ok);
    REQUIRE_FALSE(core::getRamp(torus, "radius").status.ok);
    REQUIRE(core::getRamp(sweep, "scaleRamp").points.size() == 2);
}

TEST_CASE_METHOD(Fixture, "An attribute parameter offers the attributes of its input", "[inspector]")
{
    const core::NodeId cube = makeNode("helix::cube");
    const core::NodeId create = makeNode("helix::attributeCreate", "make");
    const core::NodeId noise = makeNode("helix::attributeNoise", "noise");

    // Nothing wired in yet.
    REQUIRE_FALSE(core::listAttributeSuggestions(noise, "name").status.ok);

    REQUIRE(core::connectNodes(cube, 0, create, 0).ok);
    REQUIRE(core::connectNodes(create, 0, noise, 0).ok);
    REQUIRE(core::setString(create, "name", 0, "temperature").ok);
    REQUIRE(core::setString(create, "type", 0, "float").ok);

    core::AttributeNames names = core::listAttributeSuggestions(noise, "name");
    REQUIRE(names.status.ok);
    REQUIRE(std::count(names.names.begin(), names.names.end(), "temperature") == 1);
    // Position is internal, and the noise node asks for float attributes only.
    REQUIRE(std::count(names.names.begin(), names.names.end(), "Position") == 0);

    // The types follow the type dropdown of the node: a vector noise offers vectors.
    REQUIRE(core::setString(noise, "type", 0, "vector").ok);
    names = core::listAttributeSuggestions(noise, "name");
    REQUIRE(names.status.ok);
    REQUIRE(std::count(names.names.begin(), names.names.end(), "temperature") == 0);

    REQUIRE_FALSE(core::listAttributeSuggestions(noise, "octaves").status.ok);
}
