#include "../teq_core.h"
#include "../color_management.h"
#include "../exposure.h"
#include "../white_balance.h"
#include "../regularization.h"
#include <chrono>
#include <cmath>
#include <cstdio>
using namespace teq;

static int failures = 0;

static void check(bool ok, const char *msg)
{
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++failures;
    }
}

// Synthetic scene: dark left half / bright right half with a gradient + edge.
static Plane makeScene(int W, int H)
{
    Plane Y(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float base = x < W / 2 ? 0.02f : 0.6f;
            Y.row(y)[x] = base * (0.7f + 0.6f * float(y) / H);
        }
    return Y;
}

static float run(const Params &pp, const Plane &scene, float px, float py,
                 bool timeit = false)
{
    Plane Y = scene;
    ToneEqualizer eq(pp);
    const float gain = ToneEqualizer::pivotGain(pp.pivot);
    for (auto &v : Y.v) v = std::min(std::max(v * gain, 1e-5f), 32.f);
    auto t0 = std::chrono::steady_clock::now();
    eq.filterMask(Y, 1.0);
    auto t1 = std::chrono::steady_clock::now();
    if (timeit)
        std::printf("  filterMask %dx%d: %.0f ms\n", Y.w, Y.h,
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
    for (float v : Y.v)
        if (!std::isfinite(v)) {
            std::printf("NON-FINITE mask\n");
            ++failures;
            return -1;
        }
    return eq.correction(Y.row(int(py))[int(px)]);
}

int main()
{
    // Scene-linear photographic exposure: one stop is exactly a factor of two.
    check(bg::exposureGain(0.0) == 1.f,
          "0 EV exposure gain should be identity");
    check(std::fabs(bg::exposureGain(1.0) - 2.f) < 1e-7f,
          "+1 EV exposure gain should be 2x");
    check(std::fabs(bg::exposureGain(-1.0) - 0.5f) < 1e-7f,
          "-1 EV exposure gain should be 0.5x");
    check(std::fabs(bg::exposureGain(2.0) - 4.f) < 1e-7f,
          "+2 EV exposure gain should be 4x");
    check(std::fabs(bg::applyExposure(0.18f, bg::exposureGain(1.0)) - 0.36f) < 1e-7f,
          "+1 EV should map linear 18% grey to 36%");

    // White balance: zero Temp/Tint must be identity in every supported gamut
    // for both methods. Non-zero controls must map neutral RGB to the same
    // target white regardless of the selected adaptation method.
    for (int g = 0; g < int(bg::kGamutCount); ++g) {
        for (int method = 0; method < int(bg::kWhiteBalanceMethodCount); ++method) {
            const bg::Mat3 id = bg::makeWhiteBalanceTransform(g, method, 0.0, 0.0);
            float r, gg, b;
            bg::applyWhiteBalance(id, 0.23f, 0.41f, 0.79f, r, gg, b);
            check(std::fabs(r - 0.23f) < 2e-5f &&
                  std::fabs(gg - 0.41f) < 2e-5f &&
                  std::fabs(b - 0.79f) < 2e-5f,
                  "zero Temp/Tint should be identity for every gamut and method");

            const double temp = 40.0;
            const double tint = 25.0;
            const bg::Mat3 wb = bg::makeWhiteBalanceTransform(g, method, temp, tint);
            bg::applyWhiteBalance(wb, 1.f, 1.f, 1.f, r, gg, b);
            const bg::Vec3 target = bg::targetWhiteRGB(g, temp, tint);
            check(std::fabs(double(r) - target.v[0]) < 3e-5 &&
                  std::fabs(double(gg) - target.v[1]) < 3e-5 &&
                  std::fabs(double(b) - target.v[2]) < 3e-5,
                  "both white-balance methods should map neutral to the same target white");
        }
    }
    {
        const bg::Vec3 warm = bg::targetWhiteRGB(bg::kGamutDWG, 50.0, 0.0);
        const bg::Vec3 cool = bg::targetWhiteRGB(bg::kGamutDWG, -50.0, 0.0);
        check(warm.v[0] / warm.v[2] > cool.v[0] / cool.v[2],
              "positive Temperature should be warmer than negative Temperature");
    }
    {
        // Temperature remains one mired per unit across the extended range.
        const double refMired = 1.0e6 / bg::kReferenceCCT[bg::kGamutDWG];
        const bg::WhitePoint normalWarm = bg::targetWhitePoint(bg::kGamutDWG, 100.0, 0.0);
        const double expected100K = 1.0e6 / (refMired + 100.0);
        check(std::fabs(normalWarm.cct - expected100K) < 1e-6,
              "+100 Temperature should retain the original one-mired-per-unit response");

        const bg::WhitePoint maxCool = bg::targetWhitePoint(bg::kGamutDWG, -115.0, 0.0);
        const bg::WhitePoint maxWarm = bg::targetWhitePoint(bg::kGamutDWG, 450.0, 0.0);
        check(std::fabs(maxCool.cct - 25000.0) < 1e-6,
              "extended cool range should reach the 25000 K model limit");
        check(std::fabs(maxWarm.cct - 1667.0) < 1e-6,
              "extended warm range should reach the 1667 K model limit");

        const bg::Vec3 tintPlus = bg::targetWhiteRGB(bg::kGamutDWG, 0.0, 200.0);
        const bg::Vec3 tintMinus = bg::targetWhiteRGB(bg::kGamutDWG, 0.0, -200.0);
        check(std::isfinite(tintPlus.v[0]) && std::isfinite(tintPlus.v[1]) &&
              std::isfinite(tintPlus.v[2]) && std::isfinite(tintMinus.v[0]) &&
              std::isfinite(tintMinus.v[1]) && std::isfinite(tintMinus.v[2]),
              "extended +/-200 Tint endpoints should remain finite");
    }

    // Every luminance row should map neutral RGB (R=G=B) to the same neutral Y.
    for (int g = 0; g < int(bg::kGamutCount); ++g) {
        const float *lw = bg::lumaWeights(g);
        const float sum = lw[0] + lw[1] + lw[2];
        check(std::fabs(sum - 1.f) < 1e-4f,
              "luminance coefficients should sum to approximately 1");
    }

    // DaVinci Intermediate published reference mappings and round-trip.
    check(std::fabs(bg::encodeTransfer(0.18f, bg::kTransferDaVinciIntermediate) - 0.336043f) < 2e-6f,
          "DaVinci Intermediate should map 18% grey to 0.336043");
    check(std::fabs(bg::decodeTransfer(0.336043f, bg::kTransferDaVinciIntermediate) - 0.18f) < 2e-5f,
          "DaVinci Intermediate should decode 0.336043 to 18% grey");
    for (float x : {-0.01f, 0.f, 0.001f, 0.18f, 1.f, 10.f, 100.f}) {
        const float y = bg::encodeTransfer(x, bg::kTransferDaVinciIntermediate);
        const float z = bg::decodeTransfer(y, bg::kTransferDaVinciIntermediate);
        const float tol = std::max(2e-6f, std::fabs(x) * 2e-5f);
        check(std::fabs(z - x) < tol, "DaVinci Intermediate encode/decode should round-trip");
    }

    // ART boxblur parity: verify the recursive implementation still computes
    // the expected shrinking-window mean, including borders and in-place use.
    {
        Plane p(9, 7);
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x)
                p.row(y)[x] = 0.1f + 0.03f * x + 0.07f * y + 0.01f * x * y;
        Plane original = p;
        boxMean(p, p, 2);
        float maxErr = 0.f;
        for (int y = 0; y < p.h; ++y) {
            for (int x = 0; x < p.w; ++x) {
                float sum = 0.f;
                int n = 0;
                for (int yy = std::max(0, y - 2); yy <= std::min(p.h - 1, y + 2); ++yy)
                    for (int xx = std::max(0, x - 2); xx <= std::min(p.w - 1, x + 2); ++xx) {
                        sum += original.row(yy)[xx];
                        ++n;
                    }
                maxErr = std::max(maxErr, std::fabs(p.row(y)[x] - sum / float(n)));
            }
        }
        std::printf("boxMean border/in-place max error vs direct mean: %.9g\n", maxErr);
        check(maxErr < 2e-6f, "ART-style boxMean should match direct shrinking-window mean");
    }

    // Exact current-ART calculate_subsampling() behaviour.
    check(calcSubsampling(1920, 1080, 350) == 5,
          "350px radius should subsample by 5");
    check(calcSubsampling(1920, 1080, 7) == 1,
          "prime radius should fall through the divisor loop to subsampling 1");
    check(calcSubsampling(600, 400, 350) == 1,
          "images <=600px on the long edge should not subsample");

    const int W = 1920, H = 1080;
    Plane scene = makeScene(W, H);
    const float dx = W * 0.25f, bx = W * 0.75f, my = H / 2;

    Params id;
    const float idDark = run(id, scene, dx, my);
    const float idBright = run(id, scene, bx, my);
    std::printf("identity  dark=%.4f bright=%.4f (ART itself is ~1, not exactly 1)\n",
                idDark, idBright);
    check(std::isfinite(idDark) && std::isfinite(idBright),
          "default correction must remain finite");

    Params sh;
    sh.bands[1] = 60;
    sh.bands[0] = 40; // lift blacks+shadows
    const float shDark = run(sh, scene, dx, my, true);
    const float shBright = run(sh, scene, bx, my);
    std::printf("lift shadows: dark=%.3f bright=%.3f (expect dark>1, bright~1)\n",
                shDark, shBright);
    check(shDark > 1.f, "shadow lift should brighten dark region");

    Params hi;
    hi.bands[3] = -60;
    hi.bands[4] = -40; // pull highlights
    const float hiDark = run(hi, scene, dx, my);
    const float hiBright = run(hi, scene, bx, my);
    std::printf("pull highlights: dark=%.3f bright=%.3f (expect dark~1, bright<1)\n",
                hiDark, hiBright);
    check(hiBright < 1.f, "highlight pull should darken bright region");

    for (int reg = 0; reg <= 4; ++reg) {
        Params p = sh;
        p.regularization = reg;
        float c = run(p, scene, W / 2 - 4, my); // 4px inside dark side
        std::printf("detail=%d: correction next to edge (dark side) = %.3f\n", reg, c);
        check(std::isfinite(c), "regularization result must remain finite");
    }

    // Exercise BaseGrade's duplicated regularization body at a value that is
    // not exactly 1.0, while still snapping to ART's original 350px radius.
    // This catches future drift between regularization.h and teq_core.h.
    for (int reg = 0; reg <= 4; ++reg) {
        Params p = sh;
        p.regularization = reg;
        ToneEqualizer eq(p);
        Plane a = scene;
        Plane b = scene;
        eq.filterMask(a, 1.0);
        bg::filterMaskScaled(eq, p, b, 1.0, 1.0 + 1e-9);
        check(a.v == b.v,
              "filterMaskScaled body must match ART when the radius is unchanged");
    }

    // Away from 1.0x, the large radius should stay on multiples of five at
    // full resolution so the fast guided filter retains 5x subsampling.
    for (int i = 0; i <= 30; ++i) {
        const double scale = 0.95 + 0.005 * i;
        const int radius = bg::scaledRegularizationRadius(1.0, scale);
        check(radius % 5 == 0,
              "scaled large regularization radius should be a multiple of five");
        check(calcSubsampling(3840, 2160, radius) == 5,
              "scaled large regularization radius should retain 5x subsampling");
    }

    Params half = sh;
    Plane small = makeScene(960, 540);
    Plane Y = small;
    ToneEqualizer eq(half);
    eq.filterMask(Y, 0.5);
    const float halfCorr = eq.correction(Y.row(270)[240]);
    std::printf("half-res render OK, corr=%.3f\n", halfCorr);
    check(std::isfinite(halfCorr), "half-resolution render must remain finite");

    if (failures) {
        std::printf("%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("all smoke tests passed\n");
    return 0;
}
