#include "../contrast.h"
#include "../contrast_render_overlay.h"
#include "../color_management.h"

#include <cmath>
#include <cstdio>

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

    // Both toe directions should join continuously at the selected threshold.
    {
        const double thresholdCode = bg::toeThresholdCode(4.0);
        const float thresholdLinear = bg::decodeTransfer(float(thresholdCode),
                                                         bg::kTransferDaVinciIntermediate);
        for (double amount : {-100.0, 100.0}) {
            bg::ContrastParams p;
            p.toeStrength = amount;
            p.toeRangeEV = 4.0;
            const float at = bg::applyContrastScalar(thresholdLinear, p);
            check(std::fabs(at - thresholdLinear) < 2e-6f,
                  "toe should be continuous at its threshold");
        }
    }

    // Colour Preserve blends between per-channel curves and a luminance-ratio
    // path. The luminance path should preserve channel ratios for a safe sample.
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
              "100% Colour Preserve should preserve RGB ratios when Y is safe");
        check(std::fabs(rr - lr) + std::fabs(rg - lg) + std::fabs(rb - lb) > 1e-4f,
              "RGB and luminance contrast paths should differ on coloured input");
    }

    // DWG can produce zero/negative Y for extreme blue values. The preserve
    // path must remain finite and fall back safely instead of exploding Y'/Y.
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
