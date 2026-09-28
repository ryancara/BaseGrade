#pragma once

#include <cmath>

namespace bg {

// Photographic exposure in stops. Processing is scene-linear, so one stop is
// exactly a factor of two. This is intentionally independent of input gamut;
// the OFX wrapper decodes the selected transfer before applying the gain.
inline float exposureGain(double exposureEV)
{
    return std::exp2(float(exposureEV));
}

inline float applyExposure(float linearValue, float gain)
{
    return linearValue * gain;
}

} // namespace bg
