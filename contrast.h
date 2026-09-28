#pragma once

#include "color_management.h"

#include <algorithm>
#include <cmath>

namespace bg {

struct ContrastParams {
    double contrast = 0.0;        // -200..200, 0 = identity
    double pivotEV = 0.0;         // stops relative to 18% grey
    double softness = 0.0;        // 0..100
    double toeStrength = 0.0;     // -100..100: harden -> soften
    double toeRangeEV = 4.0;      // 1..8 stops below 18% grey
    double colourPreserve = 0.0;  // 0..100: RGB curve -> luminance-only
};

inline bool contrastIdentity(const ContrastParams &p)
{
    // Softness only shapes an existing contrast adjustment, so changing it at
    // Contrast = 0 is intentionally neutral. Toe remains an independent shaper.
    return p.contrast == 0.0 && p.toeStrength == 0.0;
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

// Values that depend only on the controls are prepared once per render. This
// avoids recomputing pow/log-derived pivot, slope and toe constants for every
// channel of every pixel.
struct PreparedContrast {
    bool identity = true;
    double pivotCode = 0.0;
    double slope = 1.0;
    double softnessK = 0.0;
    double toe = 0.0;
    double toeThreshold = 0.0;
    double toeK = 0.0;
    double preserve = 0.0;
};

inline PreparedContrast prepareContrast(const ContrastParams &p)
{
    PreparedContrast q;
    q.identity = contrastIdentity(p);
    q.pivotCode = contrastPivotCode(p.pivotEV);
    q.slope = contrastSlope(p.contrast);
    q.softnessK = 4.0 *
        (std::max(0.0, std::min(p.softness, 100.0)) / 100.0);
    q.toe = std::max(-100.0, std::min(p.toeStrength, 100.0)) / 100.0;
    q.toeThreshold = toeThresholdCode(p.toeRangeEV);
    q.toeK = 10.0 * std::fabs(q.toe);
    q.preserve = std::max(0.0, std::min(p.colourPreserve, 100.0)) / 100.0;
    return q;
}

inline double applyContrastCode(double code, const PreparedContrast &p)
{
    if (p.identity) return code;

    const double fromPivot = code - p.pivotCode;
    double delta = (p.slope - 1.0) * fromPivot;

    // Softness rolls off only the extra contrast displacement. This means it
    // cannot create a tone curve by itself when Contrast is zero, while the
    // derivative at the pivot still equals the requested contrast slope.
    if (p.softnessK > 0.0 && delta != 0.0)
        delta /= 1.0 + p.softnessK * std::fabs(fromPivot);

    double y = code + delta;

    // Independent bipolar low-end toe. Positive values compress the distance
    // below the threshold, lifting/softening the deepest shadows. Negative
    // values expand that distance, deepening/hardening the toe. Both branches
    // meet the unmodified curve with matching first derivative at the threshold.
    if (p.toe != 0.0 && y < p.toeThreshold) {
        const double dist = p.toeThreshold - y;
        const double shapedDist = p.toe > 0.0
            ? dist / (1.0 + p.toeK * dist)
            : dist * (1.0 + p.toeK * dist);
        y = p.toeThreshold - shapedDist;
    }

    return y;
}

inline double applyContrastCode(double code, const ContrastParams &p)
{
    return applyContrastCode(code, prepareContrast(p));
}

inline float applyContrastScalar(float linear, const PreparedContrast &p)
{
    if (p.identity) return linear;
    const double code = encodeTransfer(linear, kTransferDaVinciIntermediate);
    const double shaped = applyContrastCode(code, p);
    return decodeTransfer(float(shaped), kTransferDaVinciIntermediate);
}

inline float applyContrastScalar(float linear, const ContrastParams &p)
{
    return applyContrastScalar(linear, prepareContrast(p));
}

inline double smoothstep01(double a, double b, double x)
{
    const double t = std::max(0.0, std::min(1.0, (x - a) / (b - a)));
    return t * t * (3.0 - 2.0 * t);
}

inline void applyContrastRGB(const PreparedContrast &p, const float *lw,
                             float r, float g, float b,
                             float &outR, float &outG, float &outB)
{
    if (p.identity) {
        outR = r;
        outG = g;
        outB = b;
        return;
    }

    const float rgbR = applyContrastScalar(r, p);
    const float rgbG = applyContrastScalar(g, p);
    const float rgbB = applyContrastScalar(b, p);

    if (p.preserve <= 0.0) {
        outR = rgbR;
        outG = rgbG;
        outB = rgbB;
        return;
    }

    const double Y = double(lw[0]) * r + double(lw[1]) * g + double(lw[2]) * b;

    // A luminance ratio is only meaningful when Y is a sane fraction of the
    // brightest channel and is high enough for Y'/Y to express the curve near
    // black. Fade toward the ordinary RGB result rather than hard-switching,
    // which avoids discontinuities and protects saturated wide-gamut blues.
    const double mx = std::max(r, std::max(g, b));
    double confidence = 0.0;
    if (std::isfinite(Y) && Y > 0.0 && mx > 0.0) {
        confidence = smoothstep01(0.02, 0.15, Y / mx) *
                     smoothstep01(2.0e-4, 2.0e-3, Y);
    }
    const double eff = p.preserve * confidence;
    if (!(eff > 0.0)) {
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

    outR = float((1.0 - eff) * rgbR + eff * lumR);
    outG = float((1.0 - eff) * rgbG + eff * lumG);
    outB = float((1.0 - eff) * rgbB + eff * lumB);
}

inline void applyContrastRGB(const ContrastParams &p, const float *lw,
                             float r, float g, float b,
                             float &outR, float &outG, float &outB)
{
    const PreparedContrast q = prepareContrast(p);
    applyContrastRGB(q, lw, r, g, b, outR, outG, outB);
}

} // namespace bg
