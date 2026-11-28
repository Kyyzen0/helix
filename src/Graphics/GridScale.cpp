#include "Graphics/GridScale.h"
#include <cmath>
#include <initializer_list>

namespace helix::gfx {

float chooseGridCellSize(float cameraDistance)
{
    if (!(cameraDistance > 0.f) || !std::isfinite(cameraDistance)) return 1.f;

    const double wanted = double(cameraDistance) / 10.0;
    const double power = std::pow(10.0, std::floor(std::log10(wanted)));
    // Held a hair under, so a distance that is exactly 10, 20 or 50 times a step keeps that step
    // whatever the rounding of the logarithm.
    const double mantissa = wanted / power * (1.0 - 1e-9);
    for (const double step : {1.0, 2.0, 5.0})
    {
        if (mantissa <= step) return float(step * power);
    }
    return float(10.0 * power);
}

} // namespace helix::gfx
