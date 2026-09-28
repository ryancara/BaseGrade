#pragma once

#include "teq_core.h"

#include <algorithm>
#include <cmath>

namespace bg {

// BaseGrade extension to ART's regularization stage.
//
// ART uses a small ~5 px log-guided pass as conditioning, then (for
// Regularization > 1) a much larger 350 px guided pass. Regularization Scale
// intentionally changes only the large pass. This keeps ART's small-pass
// behaviour stable while letting the user choose the spatial scale over which
// local contrast is preserved.
//
// At exactly 1.0x we call ART's original filterMask() path directly, preserving
// reference behaviour. Away from 1.0x the large radius is snapped to a multiple
// of five so ART's fast guided filter keeps its 5x subsampling instead of
// unpredictably falling back to full-resolution processing for prime radii.
inline int scaledRegularizationRadius(double renderScale, double regularizationScale)
{
    const double artRadius = 350.0 * renderScale;

    // Preserve ART's exact integer truncation at the reference/default value.
    if (regularizationScale == 1.0) {
        return std::max(1, int(artRadius));
    }

    const double scaled = artRadius * std::max(0.0, regularizationScale);
    const int snapped = int(std::lround(scaled / 5.0)) * 5;
    return std::max(5, snapped);
}

inline void filterMaskScaled(const teq::ToneEqualizer &eq,
                             const teq::Params &pp,
                             teq::Plane &Y,
                             double renderScale,
                             double regularizationScale)
{
    // Reference path: bit-for-bit the same code path used by the ART clone.
    if (regularizationScale == 1.0) {
        eq.filterMask(Y, renderScale);
        return;
    }

    const int W = Y.w, H = Y.h;

    // ART's small conditioning pass is intentionally NOT affected by
    // Regularization Scale.
    const int detail = pp.regularization > 0 ? 5 : 0;
    int radius = int(float(detail) * float(renderScale) + 0.5f);
    const float epsilon = 0.01f + 0.002f * std::max(detail - 3, 0);
    if (radius > 0) {
        teq::guidedFilterLog(10.f, Y, radius, epsilon);
    }

    if (pp.regularization > 1) {
        teq::Plane Y2(W, H);
        constexpr float base_epsilon = 0.004f;
        constexpr float base_posterization = 5.f;
        constexpr float exposure_lo = -16.f;
        constexpr float exposure_hi = 6.f;
        const float inv_l2 = 1.f / std::log(2.f);

        teq::parallelRange(H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                float *yr = Y.row(y);
                float *y2 = Y2.row(y);
                for (int x = 0; x < W; ++x) {
                    const float l = std::min(
                        std::max(std::log(std::max(yr[x], 1e-9f)) * inv_l2,
                                 exposure_lo),
                        exposure_hi);
                    const float ll =
                        std::round(l * base_posterization) / base_posterization;
                    y2[x] = yr[x];
                    yr[x] = std::pow(2.f, ll);
                }
            }
        });

        radius = scaledRegularizationRadius(renderScale, regularizationScale);
        teq::guidedFilter(Y2, Y, Y, radius, base_epsilon);

        const int reg = 5 - std::min(pp.regularization, 4);
        if (reg > 1) {
            teq::guidedFilter(Y2, Y, Y, radius * (reg - 1),
                              base_epsilon / 100.f);
        }
    }
}

} // namespace bg
