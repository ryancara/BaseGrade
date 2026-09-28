#include "../contrast.h"
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

    // Exact identity when all shaping controls are neutral.
    for (float x : {-0.02f, 0.f, 0.001f, 0.18f, 1.f, 10.f}) {
        check(bg::applyContrastScalar(x, id) == x,
              "neutral contrast settings should be exact scalar identity");
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

    // Toe controls should affect deep shadows without changing tones above the
    // selected toe region.
    {
        bg::ContrastParams plain;
        bg::ContrastParams toe;
        toe.toeStrength = 80.0;
        toe.toeRangeEV = 4.0;
        const float deep = 0.002f;
        const float mid = 0.18f;
        check(bg::applyContrastScalar(deep, toe) > bg::applyContrastScalar(deep, plain),
              "Toe Strength should soften/compress deep shadows upward");
        check(std::fabs(bg::applyContrastScalar(mid, toe) - mid) < 2e-6f,
              "Toe should leave middle grey unchanged");
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

    if (failures) {
        std::printf("%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("all contrast tests passed\n");
    return 0;
}
