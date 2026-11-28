#include "Graphics/GridScale.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace helix::gfx;
using Catch::Approx;

TEST_CASE("The grid cell is a 1, 2 or 5 times a power of ten")
{
    for (float distance = 0.2f; distance < 2000.f; distance *= 1.07f)
    {
        const float cell = chooseGridCellSize(distance);
        const float mantissa = cell / std::pow(10.f, std::floor(std::log10(cell) + 1e-4f));
        const bool isStep = mantissa == Approx(1.f).epsilon(1e-3) || mantissa == Approx(2.f).epsilon(1e-3) ||
                            mantissa == Approx(5.f).epsilon(1e-3);
        INFO("distance " << distance << " gave " << cell);
        REQUIRE(isStep);
    }
}

TEST_CASE("The grid cell keeps the camera four to ten cells from its centre at any zoom")
{
    for (float distance = 0.5f; distance < 5000.f; distance *= 1.05f)
    {
        const float cells = distance / chooseGridCellSize(distance);
        INFO("distance " << distance);
        REQUIRE(cells <= 10.f + 1e-3f);
        REQUIRE(cells > 3.9f);
    }
}

TEST_CASE("The grid cell grows with the distance and steps at the known distances")
{
    REQUIRE(chooseGridCellSize(15.f) == Approx(2.f));
    REQUIRE(chooseGridCellSize(10.f) == Approx(1.f));
    REQUIRE(chooseGridCellSize(10.5f) == Approx(2.f));
    REQUIRE(chooseGridCellSize(20.f) == Approx(2.f));
    REQUIRE(chooseGridCellSize(50.f) == Approx(5.f));
    REQUIRE(chooseGridCellSize(51.f) == Approx(10.f));
    REQUIRE(chooseGridCellSize(1.f) == Approx(0.1f));
    REQUIRE(chooseGridCellSize(3.f) == Approx(0.5f));

    float previous = 0.f;
    for (float distance = 0.1f; distance < 1000.f; distance *= 1.03f)
    {
        const float cell = chooseGridCellSize(distance);
        REQUIRE(cell >= previous);
        previous = cell;
    }
}

TEST_CASE("A distance that is not a positive number gives a cell of one")
{
    REQUIRE(chooseGridCellSize(0.f) == 1.f);
    REQUIRE(chooseGridCellSize(-3.f) == 1.f);
    REQUIRE(chooseGridCellSize(std::numeric_limits<float>::quiet_NaN()) == 1.f);
    REQUIRE(chooseGridCellSize(std::numeric_limits<float>::infinity()) == 1.f);
}
