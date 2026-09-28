#include "../teq_core.h"
#include "../color_management.h"
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
    // ART's loop includes divisor 1, so positive integer radii always return
    // before the fallback. A prime radius therefore subsamples by 1.
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
