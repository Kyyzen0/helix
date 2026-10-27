#include "Engine/Async/CookPlan.h"
#include "Engine/Core/Types.h"
#include "Engine/NetworkGraph/NodeLink.h"
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <stdexcept>
#include <vector>

using namespace helix;

namespace {

using Wiring = std::map<nt::NodeId, std::vector<nt::NodeLink>>;

nt::NodeLink link(nt::NodeId source, nt::NodeId target, unsigned int input = 0)
{
    return nt::NodeLink{source, 0, target, input};
}

async::CookPlan buildPlan(const Wiring& wiring, const std::vector<nt::NodeId>& targets)
{
    return async::CookPlan::build(
        targets,
        [&](nt::NodeId nodeId) {
            const auto found = wiring.find(nodeId);
            return found == wiring.end() ? std::vector<nt::NodeLink>{} : found->second;
        },
        [&](nt::NodeId nodeId) {
            const auto found = wiring.find(nodeId);
            return found == wiring.end() ? 0u : static_cast<unsigned int>(found->second.size());
        }
    );
}

} // namespace

TEST_CASE("a cook plan groups a diamond into sorted levels")
{
    // 1 feeds 2, 3 and (twice) 5; 2 and 3 feed 4; 4 feeds 5. Node 9 feeds 1 but
    // so does nothing else, and node 7 is not reachable from the target.
    Wiring wiring;
    wiring[2] = {link(1, 2)};
    wiring[3] = {link(1, 3)};
    wiring[4] = {link(3, 4, 0), link(2, 4, 1)};
    wiring[5] = {link(4, 5, 0), link(1, 5, 1), link(1, 5, 2)};
    wiring[1] = {link(9, 1)};
    wiring[7] = {link(1, 7)};

    const async::CookPlan plan = buildPlan(wiring, {5});

    const std::vector<std::vector<nt::NodeId>> expected = {{9}, {1}, {2, 3}, {4}, {5}};
    REQUIRE(plan.levels() == expected);
    REQUIRE(plan.size() == 6);
    REQUIRE(plan.find(7) == nullptr);
    REQUIRE(plan.find(5)->inputs.size() == 3);
    REQUIRE(plan.find(5)->inputCount == 3);
    REQUIRE(plan.nodes() == std::vector<nt::NodeId>{9, 1, 2, 3, 4, 5});
}

TEST_CASE("a cook plan puts independent targets in the same level")
{
    Wiring wiring;
    wiring[8] = {link(3, 8)};
    wiring[4] = {link(3, 4)};

    const async::CookPlan plan = buildPlan(wiring, {8, 4});

    const std::vector<std::vector<nt::NodeId>> expected = {{3}, {4, 8}};
    REQUIRE(plan.levels() == expected);
}

TEST_CASE("a cook plan refuses a cycle")
{
    Wiring wiring;
    wiring[1] = {link(2, 1)};
    wiring[2] = {link(1, 2)};

    REQUIRE_THROWS_AS(buildPlan(wiring, {1}), std::runtime_error);

    Wiring selfLoop;
    selfLoop[1] = {link(1, 1)};
    REQUIRE_THROWS_AS(buildPlan(selfLoop, {1}), std::runtime_error);
}
