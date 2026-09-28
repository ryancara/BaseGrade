/* -*- C++ -*-
 *
 *  Host-independent core of the ART tone equalizer, for the OFX port.
 *
 *  Derived from ART (rtengine/iptoneequalizer.cc, rtengine/guidedfilter.cc):
 *    Copyright 2018-2019 Alberto Griggio <alberto.griggio@gmail.com>
 *  which is in turn adapted from the darktable tone equalizer:
 *    Copyright 2018 Aurelien Pierre
 *  and RawTherapee (Gabor Horvath et al.).
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 3 of the License, or (at your
 *  option) any later version.  It is distributed WITHOUT ANY WARRANTY.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace teq {

// ---------------------------------------------------------------- params ----

struct Params {
    std::array<int, 5> bands = {0, 0, 0, 0, 0}; // blacks..whites, -100..100
    double pivot = 0.0;                         // EV, -12..12
    int regularization = 4;                     // "Detail" slider, 0..4
};

// ------------------------------------------------------------- utilities ----

struct Plane {
    int w = 0, h = 0;
    std::vector<float> v;
    Plane() = default;
    Plane(int w_, int h_) : w(w_), h(h_), v(size_t(w_) * size_t(h_)) {}
    float *row(int y) { return v.data() + size_t(y) * w; }
    const float *row(int y) const { return v.data() + size_t(y) * w; }
};

// Split [0, n) across threads; f(begin, end).
template <class F> void parallelRange(int n, F f)
{
    int nt = int(std::max(1u, std::thread::hardware_concurrency()));
    nt = std::min(nt, std::max(1, n / 16));
    if (nt <= 1) {
        f(0, n);
        return;
    }
    std::vector<std::thread> ts;
    ts.reserve(nt);
    for (int i = 0; i < nt; ++i) {
        const int a = int(int64_t(n) * i / nt);
        const int b = int(int64_t(n) * (i + 1) / nt);
        ts.emplace_back([&f, a, b] { f(a, b); });
    }
    for (auto &t : ts) {
        t.join();
    }
}

// -------------------------------------------------- fast guided filter ------
// Follows Algorithm 2 of He & Sun, "Fast Guided Filter", as in ART.

inline float bilinear(const Plane &p, float x, float y)
{
    x = std::min(std::max(x, 0.f), float(p.w - 1));
    y = std::min(std::max(y, 0.f), float(p.h - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, p.w - 1), y1 = std::min(y0 + 1, p.h - 1);
    const float fx = x - x0, fy = y - y0;
    const float a = p.row(y0)[x0] * (1.f - fx) + p.row(y0)[x1] * fx;
    const float b = p.row(y1)[x0] * (1.f - fx) + p.row(y1)[x1] * fx;
    return a * (1.f - fy) + b * fy;
}

inline void resampleBilinear(const Plane &s, Plane &d)
{
    const float sx = float(s.w) / d.w, sy = float(s.h) / d.h;
    parallelRange(d.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float *dr = d.row(y);
            for (int x = 0; x < d.w; ++x) {
                dr[x] = bilinear(s, x * sx, y * sy);
            }
        }
    });
}

// ART's boxblur() mean filter, preserving its shrinking-border behaviour and
// float recurrence order. `d` may alias `s`.
inline void boxMean(const Plane &s, Plane &d, int rad)
{
    const int w = s.w, h = s.h;
    rad = std::max(0, std::min(rad, (std::min(w, h) - 1) / 2 - 1));

    if (d.w != w || d.h != h) {
        d = Plane(w, h);
    }

    // ART's array2D guided-filter path calls the six-argument boxblur()
    // overload. It performs the horizontal pass first, then the vertical pass,
    // using single-precision recursive sliding means and a window that shrinks
    // at the image borders. Keep an intermediate plane so aliasing is safe while
    // retaining the same arithmetic order.
    Plane tmp(w, h);

    if (rad == 0) {
        parallelRange(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                std::copy(s.row(y), s.row(y) + w, tmp.row(y));
            }
        });
    } else {
        parallelRange(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float *sr = s.row(y);
                float *tr = tmp.row(y);

                float len = float(rad + 1);
                float tempval = sr[0];
                for (int j = 1; j <= rad; ++j) {
                    tempval += sr[j];
                }
                tempval /= len;
                tr[0] = tempval;

                for (int x = 1; x <= rad; ++x) {
                    tempval = (tempval * len + sr[x + rad]) / (len + 1.f);
                    tr[x] = tempval;
                    len += 1.f;
                }

                for (int x = rad + 1; x < w - rad; ++x) {
                    tempval = tempval +
                              (sr[x + rad] - sr[x - rad - 1]) / len;
                    tr[x] = tempval;
                }

                for (int x = w - rad; x < w; ++x) {
                    tempval =
                        (tempval * len - sr[x - rad - 1]) / (len - 1.f);
                    tr[x] = tempval;
                    len -= 1.f;
                }
            }
        });
    }

    if (rad == 0) {
        parallelRange(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                std::copy(tmp.row(y), tmp.row(y) + w, d.row(y));
            }
        });
        return;
    }

    // ART parallelises this pass by columns. Each column is independent.
    parallelRange(w, [&](int x0, int x1) {
        for (int x = x0; x < x1; ++x) {
            float len = float(rad + 1);
            float tempval = tmp.row(0)[x];
            for (int y = 1; y <= rad; ++y) {
                tempval += tmp.row(y)[x];
            }
            tempval /= len;
            d.row(0)[x] = tempval;

            for (int y = 1; y <= rad; ++y) {
                tempval =
                    (tempval * len + tmp.row(y + rad)[x]) / (len + 1.f);
                d.row(y)[x] = tempval;
                len += 1.f;
            }

            const float rlen = 1.f / len;
            for (int y = rad + 1; y < h - rad; ++y) {
                tempval = tempval +
                          (tmp.row(y + rad)[x] - tmp.row(y - rad - 1)[x]) * rlen;
                d.row(y)[x] = tempval;
            }

            for (int y = h - rad; y < h; ++y) {
                tempval =
                    (tempval * len - tmp.row(y - rad - 1)[x]) / (len - 1.f);
                d.row(y)[x] = tempval;
                len -= 1.f;
            }
        }
    });
}

// Same integer arithmetic as ART's calculate_subsampling().
inline int calcSubsampling(int w, int h, int r)
{
    if (r == 1 || std::max(w, h) <= 600) {
        return 1;
    }
    for (int s = 5; s > 0; --s) {
        if (r % s == 0) {
            return s;
        }
    }
    // ART fallback: LIM(r / 2, 2, 4).
    return std::max(2, std::min(r / 2, 4));
}

// q = a*I + b.  `q` may alias `I` and/or `p`.
inline void guidedFilter(const Plane &I, const Plane &p, Plane &q, int r,
                         float epsilon)
{
    const int W = p.w, H = p.h;
    const int ss = calcSubsampling(W, H, r);
    const int w = std::max(1, W / ss), h = std::max(1, H / ss);

    Plane I1(w, h), p1(w, h);
    if (w == W && h == H) {
        I1.v = I.v;
        p1.v = p.v;
    } else {
        resampleBilinear(I, I1);
        resampleBilinear(p, p1);
    }
    const int rad = int(float(r) / ss); // ART truncates here too
    const size_t n = size_t(w) * h;

    Plane meanI(w, h), meanp(w, h), corrIp(w, h), corrI(w, h);
    boxMean(I1, meanI, rad);
    boxMean(p1, meanp, rad);
    for (size_t i = 0; i < n; ++i) corrIp.v[i] = I1.v[i] * p1.v[i];
    boxMean(corrIp, corrIp, rad);
    for (size_t i = 0; i < n; ++i) corrI.v[i] = I1.v[i] * I1.v[i];
    boxMean(corrI, corrI, rad);

    Plane &a = corrI; // reuse: a = cov / (var + eps)
    Plane b(w, h);
    for (size_t i = 0; i < n; ++i) {
        const float var = corrI.v[i] - meanI.v[i] * meanI.v[i];
        const float cov = corrIp.v[i] - meanI.v[i] * meanp.v[i];
        const float ai = cov / (var + epsilon);
        a.v[i] = ai;
        b.v[i] = meanp.v[i] - ai * meanI.v[i];
    }
    boxMean(a, a, rad);
    boxMean(b, b, rad);

    const float cs = float(w) / float(q.w), rs = float(h) / float(q.h);
    parallelRange(q.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float yy = y * rs;
            const float *Ir = I.row(y);
            float *qr = q.row(y);
            for (int x = 0; x < q.w; ++x) {
                qr[x] = bilinear(a, x * cs, yy) * Ir[x] + bilinear(b, x * cs, yy);
            }
        }
    });
}

// xlin2log / xlog2lin as in RawTherapee's sleef.h.
inline float lin2log(float x, float base)
{
    return std::log(x * (base - 1.f) + 1.f) / std::log(base);
}
inline float log2lin(float x, float base)
{
    return (std::pow(base, x) - 1.f) / (base - 1.f);
}

inline void guidedFilterLog(float base, Plane &chan, int r, float eps)
{
    for (auto &x : chan.v) x = lin2log(std::max(x, 0.f), base);
    guidedFilter(chan, chan, chan, r, eps);
    for (auto &x : chan.v) x = log2lin(std::max(x, 0.f), base);
}

// ------------------------------------------------------ tone equalizer ------

class ToneEqualizer {
public:
    static constexpr int NB = 12;
    static constexpr float LUMA_LO = -14.f, LUMA_HI = 4.f;
    static constexpr int ART_LUT_N = 65536;
    static constexpr int ART_COLORMAP_LUT_N = 65537;

    explicit ToneEqualizer(const Params &pp) : pp_(pp)
    {
        const auto conv = [](int v, float lo, float hi) -> float {
            return std::pow(2.f, float(v) / 100.f * (v < 0 ? lo : hi));
        };
        const auto &b = pp.bands;
        factors_ = {
            conv(b[0], 2.f, 3.f),   conv(b[0], 2.f, 3.f), conv(b[0], 2.f, 3.f),
            conv(b[0], 2.f, 3.f),   conv(b[0], 2.f, 3.f), // -16 .. -8 EV
            conv(b[1], 2.f, 3.f),                         // -6
            conv(b[2], 2.5f, 2.5f),                       // -4
            conv(b[3], 3.f, 2.f),                         // -2
            conv(b[4], 3.f, 2.f),   conv(b[4], 3.f, 2.f), // 0 .. 6
            conv(b[4], 3.f, 2.f),   conv(b[4], 3.f, 2.f)};

        // ART normalises the Gaussian bank against its sum at 0 EV.
        wsum_ = 0.f;
        for (int i = 0; i < NB; ++i) wsum_ += gauss(centers()[i], 0.f);

        // ART builds a 65536-entry LUT uniformly in *linear luminance* for
        // Y <= 1, and computes the correction directly for Y > 1.
        lutCorr_.resize(ART_LUT_N);
        for (int i = 0; i < ART_LUT_N; ++i) {
            const float y = float(i) / 65535.f;
            lutCorr_[i] = processCorrectionDirect(y);
        }

        // ART's colour-map preview uses the same 0..1 linear-luminance LUT,
        // plus one final saturated-white-band entry for values above 1.
        // ART ICC-converts these display colours to its working profile. This
        // standalone core uses linearised sRGB approximations; this affects
        // preview colours only, never the tone-equalizer correction itself.
        lutColor_.resize(ART_COLORMAP_LUT_N);
        for (int i = 0; i < ART_LUT_N; ++i) {
            const float y = float(i) / 65535.f;
            lutColor_[i] = processColorDirect(y);
        }
        lutColor_[ART_LUT_N] = {
            srgbToLin(1.f), srgbToLin(0.f), srgbToLin(0.f)};
    }

    // Exposure-like gain applied to the luminance before building the mask.
    static float pivotGain(double pivot) { return float(std::pow(2.0, -pivot)); }

    // Smooth (edge-aware) the pivot-scaled, clamped luminance in place.
    // renderScale is OFX's scale (1.0 at full resolution, 0.5 at half res).
    // ART expresses the same quantity inversely as its internal `scale`.
    void filterMask(Plane &Y, double renderScale) const
    {
        const int W = Y.w, H = Y.h;
        const int detail = pp_.regularization > 0 ? 5 : 0;
        int radius = int(float(detail) * float(renderScale) + 0.5f);
        float epsilon = 0.01f + 0.002f * std::max(detail - 3, 0);
        if (radius > 0) {
            guidedFilterLog(10.f, Y, radius, epsilon);
        }

        if (pp_.regularization > 1) {
            Plane Y2(W, H);
            constexpr float base_epsilon = 0.004f;
            constexpr float base_posterization = 5.f;
            const float inv_l2 = 1.f / std::log(2.f);
            parallelRange(H, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    float *yr = Y.row(y);
                    float *y2 = Y2.row(y);
                    for (int x = 0; x < W; ++x) {
                        const float l = std::min(
                            std::max(std::log(std::max(yr[x], 1e-9f)) * inv_l2,
                                     centers()[0]),
                            centers()[NB - 1]);
                        const float ll =
                            std::round(l * base_posterization) / base_posterization;
                        y2[x] = yr[x];
                        yr[x] = std::pow(2.f, ll);
                    }
                }
            });

            // Current ART: radius = 350 / scale. With OFX renderScale =
            // 1 / ART-scale, this is exactly 350 * renderScale.
            radius = int(350.f * float(renderScale));
            guidedFilter(Y2, Y, Y, radius, base_epsilon);

            const int reg = 5 - std::min(pp_.regularization, 4);
            if (reg > 1) {
                guidedFilter(Y2, Y, Y, radius * (reg - 1), base_epsilon / 100.f);
            }
        }
    }

    // Multiplicative correction for a filtered mask value. This mirrors ART's
    // LUT path for Y <= 1 and direct evaluation for values above 1.
    float correction(float y) const
    {
        if (y > 1.f) {
            return processCorrectionDirect(y);
        }
        const float pos = std::max(y, 0.f) * 65535.f;
        int i = int(pos);
        if (i >= ART_LUT_N - 1) return lutCorr_.back();
        const float f = pos - float(i);
        return lutCorr_[i] + (lutCorr_[i + 1] - lutCorr_[i]) * f;
    }

    // Colour-map view. ART's mask positions are reproduced, while the preview
    // colours themselves are only an approximation because ART uses ICC colour
    // management for this diagnostic display.
    void color(float y, float out[3]) const
    {
        const float pos = std::max(y, 0.f) * 65535.f;
        if (pos >= float(ART_LUT_N)) {
            for (int k = 0; k < 3; ++k) out[k] = lutColor_[ART_LUT_N][k];
            return;
        }
        const int i = std::min(int(pos), ART_LUT_N - 1);
        const float f = pos - float(i);
        const int j = std::min(i + 1, ART_LUT_N);
        for (int k = 0; k < 3; ++k) {
            out[k] = lutColor_[i][k] + (lutColor_[j][k] - lutColor_[i][k]) * f;
        }
    }

private:
    static const float *centers()
    {
        static const float c[NB] = {-16.f, -14.f, -12.f, -10.f, -8.f, -6.f,
                                    -4.f,  -2.f,  0.f,   2.f,   4.f,  6.f};
        return c;
    }

    static float gauss(float b, float x)
    {
        return std::exp(-(x - b) * (x - b) / 4.f);
    }

    static float exposurePosition(float y)
    {
        if (!(y > 0.f)) return LUMA_LO;
        return std::min(std::max(std::log2(y), LUMA_LO), LUMA_HI);
    }

    float processCorrectionDirect(float y) const
    {
        const float luma = exposurePosition(y);
        float correction = 0.f;
        for (int c = 0; c < NB; ++c) {
            correction += gauss(centers()[c], luma) * factors_[c];
        }
        return correction / wsum_;
    }

    static float srgbToLin(float c)
    {
        return c <= 0.04045f ? c / 12.92f
                             : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    std::array<float, 3> processColorDirect(float y) const
    {
        static const float cm[NB][3] = {
            {.5f, 0.f, .5f}, {.5f, 0.f, .5f}, {.5f, 0.f, .5f}, {.5f, 0.f, .5f},
            {.5f, 0.f, .5f},                                        // blacks
            {0.f, 0.f, 1.f},                                        // shadows
            {.5f, .5f, .5f},                                        // midtones
            {1.f, 1.f, 0.f},                                        // highlights
            {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f},
            {1.f, 0.f, 0.f}};                                       // whites

        const float luma = exposurePosition(y);
        std::array<float, 3> ret = {0.f, 0.f, 0.f};
        for (int c = 0; c < NB; ++c) {
            const float w = gauss(centers()[c], luma);
            for (int k = 0; k < 3; ++k) ret[k] += w * srgbToLin(cm[c][k]);
        }
        for (int k = 0; k < 3; ++k) {
            ret[k] = std::min(std::max(ret[k] / wsum_, 0.f), 1.f);
        }
        return ret;
    }

    Params pp_;
    std::array<float, NB> factors_{};
    float wsum_ = 1.f;
    std::vector<float> lutCorr_;
    std::vector<std::array<float, 3>> lutColor_;
};
} // namespace teq
