#include "../contrast.h"
#include "../contrast_render_overlay.h"
#include "../color_management.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

static int failures = 0;

static void check(bool ok, const char *msg)
{
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++failures;
    }
}

int main()
{
    bg::ContrastParams id;
    const float *dwg = bg::lumaWeights(bg::kGamutDWG);
    const float *rec2020 = bg::lumaWeights(bg::kGamutRec2020);

    // Exact identity when contrast and toe are neutral. Curve Softness should
    // not create its own curve when Contrast is zero.
    for (float x : {-0.02f, 0.f, 0.001f, 0.18f, 1.f, 10.f}) {
        check(bg::applyContrastScalar(x, id) == x,
              "neutral contrast settings should be exact scalar identity");
        bg::ContrastParams softOnly = id;
        softOnly.softness = 100.0;
        check(bg::applyContrastScalar(x, softOnly) == x,
              "Curve Softness alone should be exact identity");
    }
    {
        float r, g, b;
        bg::applyContrastRGB(id, dwg, -0.01f, 0.18f, 2.0f, r, g, b);
        check(r == -0.01f && g == 0.18f && b == 2.0f,
              "neutral contrast settings should be exact RGB identity");
    }

    // Prepared render constants must reproduce the public convenience path.
    {
        bg::ContrastParams p;
        p.contrast = 73.0;
        p.pivotEV = -0.7;
        p.softness = 41.0;
        p.toeStrength = -28.0;
        p.toeRangeEV = 5.2;
        p.colourPreserve = 67.0;
        const bg::PreparedContrast q = bg::prepareContrast(p);

        for (float x : {-0.01f, 0.0003f, 0.02f, 0.18f, 0.7f, 3.0f})
            check(bg::applyContrastScalar(x, p) == bg::applyContrastScalar(x, q),
                  "prepared scalar contrast should be bit-identical");

        float ar, ag, ab, br, bgc, bb;
        bg::applyContrastRGB(p, dwg, 0.42f, 0.18f, 0.08f, ar, ag, ab);
        bg::applyContrastRGB(q, dwg, 0.42f, 0.18f, 0.08f, br, bgc, bb);
        check(ar == br && ag == bgc && ab == bb,
              "prepared RGB contrast should be bit-identical");
    }

    // Pivoted contrast must leave its pivot fixed while separating values
    // above and below it.
    {
        bg::ContrastParams p;
        p.contrast = 100.0; // 2x local slope
        const float pivot = float(bg::contrastPivotLinear(0.0));
        const float dark = 0.09f;
        const float bright = 0.36f;
        const float pOut = bg::applyContrastScalar(pivot, p);
        const float dOut = bg::applyContrastScalar(dark, p);
        const float bOut = bg::applyContrastScalar(bright, p);
        check(std::fabs(pOut - pivot) < 2e-6f,
              "contrast pivot should remain fixed");
        check(dOut < dark, "positive contrast should darken below the pivot");
        check(bOut > bright, "positive contrast should brighten above the pivot");
    }

    // Toe Range is measured below Contrast Pivot in the input domain, so an
    // active toe can never reach or move the pivot itself.
    {
        bool fixed = true;
        for (double pivotEV : {-6.0, -3.0, 0.0, 3.0, 6.0})
            for (double range : {1.0, 4.0, 8.0})
                for (double toe : {-100.0, -40.0, 40.0, 100.0})
                    for (double contrast : {-100.0, 100.0}) {
                        bg::ContrastParams p;
                        p.pivotEV = pivotEV;
                        p.toeRangeEV = range;
                        p.toeStrength = toe;
                        p.contrast = contrast;
                        p.softness = 30.0;
                        const float pivot = float(bg::contrastPivotLinear(pivotEV));
                        const float out = bg::applyContrastScalar(pivot, p);
                        if (std::fabs(out - pivot) > 2e-5f * std::max(pivot, 1.0e-6f))
                            fixed = false;
                    }
        check(fixed, "active toe must not move the contrast pivot");
    }

    // The numerical code-domain slope at the pivot should match the requested
    // contrast slope at every softness when Toe Amount is neutral.
    {
        bool ok = true;
        constexpr double eps = 1.0e-6;
        for (double softness : {0.0, 25.0, 50.0, 100.0}) {
            for (double contrast : {-100.0, -25.0, 50.0, 100.0}) {
                for (double pivotEV : {-2.0, 0.0, 2.0}) {
                    bg::ContrastParams p;
                    p.contrast = contrast;
                    p.softness = softness;
                    p.pivotEV = pivotEV;
                    const bg::PreparedContrast q = bg::prepareContrast(p);
                    const double x = q.pivotCode;
                    const double y0 = bg::applyContrastCode(x - eps, q);
                    const double y1 = bg::applyContrastCode(x + eps, q);
                    const double slope = (y1 - y0) / (2.0 * eps);
                    if (!std::isfinite(slope) || std::fabs(slope - q.slope) > 2.0e-5)
                        ok = false;
                }
            }
        }
        check(ok, "contrast slope at pivot should match requested slope");
    }

    // Curve Softness should reduce extreme movement while preserving the
    // pivot and local contrast intent.
    {
        bg::ContrastParams hard;
        hard.contrast = 100.0;
        bg::ContrastParams soft = hard;
        soft.softness = 75.0;
        const float x = 2.0f;
        const float hardOut = bg::applyContrastScalar(x, hard);
        const float softOut = bg::applyContrastScalar(x, soft);
        check(softOut < hardOut,
              "Curve Softness should roll off strong highlight expansion");
        const float pivot = float(bg::contrastPivotLinear(0.0));
        check(std::fabs(bg::applyContrastScalar(pivot, soft) - pivot) < 2e-6f,
              "Curve Softness should keep the pivot fixed");
    }

    // Bipolar Toe Amount should soften/lift in the positive direction and
    // deepen/harden in the negative direction, while leaving tones above the
    // selected toe region unchanged.
    {
        bg::ContrastParams plain;
        bg::ContrastParams softToe;
        softToe.toeStrength = 80.0;
        softToe.toeRangeEV = 4.0;
        bg::ContrastParams hardToe = softToe;
        hardToe.toeStrength = -80.0;

        const float deep = 0.002f;
        const float mid = 0.18f;
        const float plainDeep = bg::applyContrastScalar(deep, plain);
        const float softDeep = bg::applyContrastScalar(deep, softToe);
        const float hardDeep = bg::applyContrastScalar(deep, hardToe);

        check(softDeep > plainDeep,
              "positive Toe Amount should soften/lift deep shadows");
        check(hardDeep < plainDeep,
              "negative Toe Amount should deepen/harden deep shadows");
        check(std::fabs(bg::applyContrastScalar(mid, softToe) - mid) < 2e-6f &&
              std::fabs(bg::applyContrastScalar(mid, hardToe) - mid) < 2e-6f,
              "Toe Amount should leave middle grey unchanged");
    }

    // Toe Range selects an INPUT-domain boundary, but the toe joins at that
    // boundary after it has been mapped through Contrast/Softness. The join
    // must remain continuous for either contrast direction and either toe sign.
    {
        bool continuous = true;
        for (double contrast : {-100.0, 100.0})
            for (double softness : {0.0, 60.0})
                for (double amount : {-100.0, 100.0}) {
                    bg::ContrastParams p;
                    p.contrast = contrast;
                    p.softness = softness;
                    p.toeStrength = amount;
                    p.toeRangeEV = 4.0;
                    const bg::PreparedContrast q = bg::prepareContrast(p);
                    const float thresholdLinear = bg::decodeTransfer(
                        float(q.toeInputThreshold), bg::kTransferDaVinciIntermediate);
                    const float at = bg::applyContrastScalar(thresholdLinear, q);
                    const float expected = bg::decodeTransfer(
                        float(q.toeOutputThreshold), bg::kTransferDaVinciIntermediate);
                    if (std::fabs(at - expected) > 2e-6f)
                        continuous = false;
                }
        check(continuous, "toe should join continuously at its input-domain threshold");
    }

    // Input-relative Toe Range must keep working under negative Contrast.
    // Negative contrast may lift the mapped output boundary, but it must not
    // remove the selected input region from Toe processing.
    {
        bg::ContrastParams base;
        base.contrast = -100.0;
        base.softness = 30.0;
        base.toeRangeEV = 4.0;

        bg::ContrastParams softToe = base;
        softToe.toeStrength = 80.0;
        bg::ContrastParams hardToe = base;
        hardToe.toeStrength = -80.0;

        const float deep = float(bg::contrastPivotLinear(0.0) * std::pow(2.0, -6.0));
        const float neutral = bg::applyContrastScalar(deep, base);
        const float softened = bg::applyContrastScalar(deep, softToe);
        const float hardened = bg::applyContrastScalar(deep, hardToe);

        check(softened > neutral,
              "positive Toe Amount should remain active under negative Contrast");
        check(hardened < neutral,
              "negative Toe Amount should remain active under negative Contrast");
    }

    // Deterministic sweep across the full control ranges. The scalar curve must
    // remain monotonic as input exposure increases.
    {
        std::uint32_t state = 0x12345678u;
        auto random01 = [&]() {
            state = state * 1664525u + 1013904223u;
            return double(state) / double(std::numeric_limits<std::uint32_t>::max());
        };

        bool monotonic = true;
        for (int s = 0; s < 512 && monotonic; ++s) {
            bg::ContrastParams p;
            p.contrast = -200.0 + 400.0 * random01();
            p.pivotEV = -6.0 + 12.0 * random01();
            p.softness = 100.0 * random01();
            p.toeStrength = -100.0 + 200.0 * random01();
            p.toeRangeEV = 1.0 + 7.0 * random01();
            const bg::PreparedContrast q = bg::prepareContrast(p);

            double previous = -std::numeric_limits<double>::infinity();
            for (int i = 0; i <= 256; ++i) {
                const double ev = -12.0 + 20.0 * (double(i) / 256.0);
                const float in = float(0.18 * std::pow(2.0, ev));
                const double out = bg::applyContrastScalar(in, q);
                if (!std::isfinite(out) || out + 1.0e-7 < previous) {
                    monotonic = false;
                    break;
                }
                previous = out;
            }
        }
        check(monotonic, "contrast curve should remain monotonic across parameter sweep");
    }

    // Colour Preserve blends between per-channel curves and a luminance-ratio
    // path. Ordinary colours should preserve channel ratios at 100%.
    {
        bg::ContrastParams rgb;
        rgb.contrast = 80.0;
        rgb.colourPreserve = 0.0;
        bg::ContrastParams lum = rgb;
        lum.colourPreserve = 100.0;

        const float inR = 0.42f, inG = 0.18f, inB = 0.08f;
        float rr, rg, rb, lr, lg, lb;
        bg::applyContrastRGB(rgb, dwg, inR, inG, inB, rr, rg, rb);
        bg::applyContrastRGB(lum, dwg, inR, inG, inB, lr, lg, lb);

        check(std::fabs((lr / lg) - (inR / inG)) < 2e-5f &&
              std::fabs((lb / lg) - (inB / inG)) < 2e-5f,
              "100% Colour Preserve should preserve RGB ratios when confidence is high");
        check(std::fabs(rr - lr) + std::fabs(rg - lg) + std::fabs(rb - lb) > 1e-4f,
              "RGB and luminance contrast paths should differ on coloured input");
    }

    // A physically valid Rec.2020 blue must not be deleted by the luminance
    // path when positive contrast pushes its very low Y below zero.
    {
        bg::ContrastParams p;
        p.contrast = 100.0;
        p.colourPreserve = 100.0;
        float r, g, b;
        bg::applyContrastRGB(p, rec2020, 0.0f, 0.0f, 0.5f, r, g, b);
        check(std::isfinite(r) && std::isfinite(g) && std::isfinite(b) &&
              b > 0.1f && (r + g + b) > 0.1f,
              "100% Colour Preserve should not crush real Rec.2020 blue to black");
    }

    // The old hard Y fallback produced a large step between these neighbouring
    // DWG pixels. The confidence fade should keep them visually continuous.
    {
        bg::ContrastParams p;
        p.contrast = 100.0;
        p.colourPreserve = 100.0;
        const bg::PreparedContrast q = bg::prepareContrast(p);

        float r0, g0, b0, r1, g1, b1;
        bg::applyContrastRGB(q, dwg, 0.00235f, 0.05f, 0.30f, r0, g0, b0);
        bg::applyContrastRGB(q, dwg, 0.00236f, 0.05f, 0.30f, r1, g1, b1);
        const float jump = std::max({std::fabs(r1 - r0),
                                     std::fabs(g1 - g0),
                                     std::fabs(b1 - b0)});
        check(jump < 1.0e-3f,
              "Colour Preserve should remain continuous around low-Y wide-gamut colours");
    }

    // Near black, luminance-ratio processing cannot reproduce a lifted black
    // because 0 * gain remains 0. Fade back to RGB so negative contrast retains
    // the intended curve response rather than hitting the gain limit.
    {
        bg::ContrastParams rgb;
        rgb.contrast = -100.0;
        rgb.colourPreserve = 0.0;
        bg::ContrastParams lum = rgb;
        lum.colourPreserve = 100.0;
        float rr, rg, rb, lr, lg, lb;
        bg::applyContrastRGB(rgb, dwg, 1.0e-4f, 1.0e-4f, 1.0e-4f, rr, rg, rb);
        bg::applyContrastRGB(lum, dwg, 1.0e-4f, 1.0e-4f, 1.0e-4f, lr, lg, lb);
        check(rr == lr && rg == lg && rb == lb,
              "Colour Preserve should fade to RGB near black for negative contrast");
    }

    // DWG can produce zero/negative Y for extreme blue values. The preserve
    // path must remain finite and fall back smoothly instead of exploding Y'/Y.
    {
        bg::ContrastParams p;
        p.contrast = 100.0;
        p.colourPreserve = 100.0;
        float r, g, b;
        bg::applyContrastRGB(p, dwg, 0.0f, 0.0f, 1.0f, r, g, b);
        check(std::isfinite(r) && std::isfinite(g) && std::isfinite(b),
              "Colour Preserve should remain finite for negative/near-zero DWG luminance");
    }

    // The DCTL-style raster diagnostic must leave pixels outside its panel
    // untouched and composite visible content inside the panel.
    {
        bg::ContrastParams p;
        p.contrast = 60.0;
        p.softness = 40.0;
        p.toeStrength = 30.0;
        bg::ContrastCurveRasterOverlay overlay(
            1000, 600, bg::kTransferDaVinciIntermediate, p);
        check(overlay.valid(), "raster Show Curve overlay should initialise");

        float r = 0.3f, g = 0.4f, b = 0.5f;
        overlay.composite(0, 0, r, g, b);
        check(r == 0.3f && g == 0.4f && b == 0.5f,
              "raster overlay should not touch pixels outside its panel");

        r = 0.3f; g = 0.4f; b = 0.5f;
        overlay.composite(30, 20, r, g, b);
        check(std::fabs(r - 0.3f) + std::fabs(g - 0.4f) + std::fabs(b - 0.5f) > 1e-4f,
              "raster overlay should composite pixels inside its panel");
    }

    if (failures) {
        std::printf("%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("all contrast tests passed\n");
    return 0;
}
