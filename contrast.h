#pragma once

#include "color_management.h"

#include <algorithm>
#include <cmath>

namespace bg {

struct ContrastParams {
    double contrast = 0.0;        // -200..200, 0 = identity
    double pivotEV = 0.0;         // stops relative to 18% grey
    double softness = 0.0;        // 0..100
    double toeStrength = 0.0;     // 0..100
    double toeRangeEV = 4.0;      // 1..8 stops below 18% grey
    double colourPreserve = 0.0;  // 0..100: RGB curve -> luminance-only
};

inline bool contrastIdentity(const ContrastParams &p)
{
    return p.contrast == 0.0 && p.softness == 0.0 && p.toeStrength == 0.0;
}

inline double contrastSlope(double contrast)
{
    const double c = std::max(-200.0, std::min(contrast, 200.0));
    return std::pow(2.0, c / 100.0);
}

inline double contrastPivotLinear(double pivotEV)
{
    return 0.18 * std::pow(2.0, std::max(-6.0, std::min(pivotEV, 6.0)));
}

inline double contrastPivotCode(double pivotEV)
{
    return encodeTransfer(float(contrastPivotLinear(pivotEV)),
                          kTransferDaVinciIntermediate);
}

inline double toeThresholdCode(double toeRangeEV)
{
    const double range = std::max(1.0, std::min(toeRangeEV, 8.0));
    const double lin = 0.18 * std::pow(2.0, -range);
    return encodeTransfer(float(lin), kTransferDaVinciIntermediate);
}

inline double applyContrastCode(double code, const ContrastParams &p)
{
    if (contrastIdentity(p)) return code;

    const double pivot = contrastPivotCode(p.pivotEV);
    const double slope = contrastSlope(p.contrast);
    double d = (code - pivot) * slope;

    // Symmetric soft-sign roll-off. The derivative at the pivot is unchanged,
    // so Contrast still controls the local slope while Softness progressively
    // rounds the shoulder and lower half of the S-curve away from the pivot.
    const double softness = std::max(0.0, std::min(p.softness, 100.0)) / 100.0;
    if (softness > 0.0) {
        const double k = 4.0 * softness;
        d = d / (1.0 + k * std::fabs(d));
    }

    double y = pivot + d;

    // Independent low-end toe. The rational form is C1-continuous at the toe
    // threshold and compresses increasingly deep shadows without a hard knee.
    const double toe = std::max(0.0, std::min(p.toeStrength, 100.0)) / 100.0;
    if (toe > 0.0) {
        const double threshold = toeThresholdCode(p.toeRangeEV);
        if (y < threshold) {
            const double dist = threshold - y;
            const double k = 10.0 * toe;
            y = threshold - dist / (1.0 + k * dist);
        }
    }

    return y;
}

inline float applyContrastScalar(float linear, const ContrastParams &p)
{
    if (contrastIdentity(p)) return linear;
    const double code = encodeTransfer(linear, kTransferDaVinciIntermediate);
    const double shaped = applyContrastCode(code, p);
    return decodeTransfer(float(shaped), kTransferDaVinciIntermediate);
}

inline void applyContrastRGB(const ContrastParams &p, const float *lw,
                             float r, float g, float b,
                             float &outR, float &outG, float &outB)
{
    if (contrastIdentity(p)) {
        outR = r;
        outG = g;
        outB = b;
        return;
    }

    const float rgbR = applyContrastScalar(r, p);
    const float rgbG = applyContrastScalar(g, p);
    const float rgbB = applyContrastScalar(b, p);

    const double preserve =
        std::max(0.0, std::min(p.colourPreserve, 100.0)) / 100.0;
    if (preserve <= 0.0) {
        outR = rgbR;
        outG = rgbG;
        outB = rgbB;
        return;
    }

    const double Y = double(lw[0]) * r + double(lw[1]) * g + double(lw[2]) * b;

    // A luminance ratio is not meaningful for non-positive or near-zero Y,
    // especially in very wide gamuts with negative luminance coefficients.
    // Fall back to the regular RGB curve instead of creating a huge Y'/Y gain.
    if (!(Y > 1.0e-6) || !std::isfinite(Y)) {
        outR = rgbR;
        outG = rgbG;
        outB = rgbB;
        return;
    }

    const double shapedY = applyContrastScalar(float(Y), p);
    double gain = shapedY / Y;
    if (!std::isfinite(gain)) gain = 1.0;
    gain = std::max(0.0, std::min(gain, 64.0));

    const float lumR = float(double(r) * gain);
    const float lumG = float(double(g) * gain);
    const float lumB = float(double(b) * gain);

    outR = float((1.0 - preserve) * rgbR + preserve * lumR);
    outG = float((1.0 - preserve) * rgbG + preserve * lumG);
    outB = float((1.0 - preserve) * rgbB + preserve * lumB);
}

} // namespace bg
