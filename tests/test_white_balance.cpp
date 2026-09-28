#include "../color_management.h"
#include "../white_balance.h"

#include <algorithm>
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

static double max3(double a, double b, double c)
{
    return std::max(a, std::max(b, c));
}

int main()
{
    // The two dropdown choices must remain genuinely different transforms for
    // non-neutral colours. They should agree on the target white, not on every
    // RGB triplet.
    {
        const bg::Mat3 linear = bg::makeWhiteBalanceTransform(
            bg::kGamutDWG, bg::kWhiteBalanceLinearRGB, 60.0, 20.0);
        const bg::Mat3 bradford = bg::makeWhiteBalanceTransform(
            bg::kGamutDWG, bg::kWhiteBalanceBradford, 60.0, 20.0);

        float lr, lg, lb, br, bgc, bb;
        bg::applyWhiteBalance(linear, 0.03f, 0.05f, 0.40f, lr, lg, lb);
        bg::applyWhiteBalance(bradford, 0.03f, 0.05f, 0.40f, br, bgc, bb);

        const double diff = max3(std::fabs(double(lr) - br),
                                 std::fabs(double(lg) - bgc),
                                 std::fabs(double(lb) - bb));
        check(diff > 1e-4,
              "Linear RGB Gain and Bradford should diverge on saturated colours");
    }

    // The exact-zero identity shortcut must not conceal a discontinuity.
    for (int g = 0; g < int(bg::kGamutCount); ++g) {
        for (int method = 0; method < int(bg::kWhiteBalanceMethodCount); ++method) {
            const bg::Mat3 near = bg::makeWhiteBalanceTransform(
                g, method, 1e-6, -1e-6);
            float r, gg, b;
            bg::applyWhiteBalance(near, 0.23f, 0.41f, 0.79f, r, gg, b);
            const double err = max3(std::fabs(double(r) - 0.23),
                                    std::fabs(double(gg) - 0.41),
                                    std::fabs(double(b) - 0.79));
            check(err < 1e-6,
                  "white balance should be continuous at zero Temp/Tint");
        }
    }

    // A neutral white is moved chromatically, but its Y luminance should stay
    // at unity for all supported gamuts, methods, and representative settings.
    const double settings[][2] = {
        {60.0, 20.0},
        {-60.0, -20.0},
        {450.0, -200.0},
        {-115.0, 200.0}
    };
    for (int g = 0; g < int(bg::kGamutCount); ++g) {
        const float *lw = bg::lumaWeights(g);
        for (int method = 0; method < int(bg::kWhiteBalanceMethodCount); ++method) {
            for (const auto &s : settings) {
                const bg::Mat3 wb = bg::makeWhiteBalanceTransform(
                    g, method, s[0], s[1]);
                float r, gg, b;
                bg::applyWhiteBalance(wb, 1.f, 1.f, 1.f, r, gg, b);
                const double y = double(lw[0]) * r +
                                 double(lw[1]) * gg +
                                 double(lw[2]) * b;
                check(std::fabs(y - 1.0) < 2e-4,
                      "white balance should preserve neutral-white luminance");
            }
        }
    }

    // Positive Tint is defined as magenta and therefore reduces green relative
    // to the red/blue average compared with an equal negative Tint move.
    {
        const bg::Vec3 plus = bg::targetWhiteRGB(bg::kGamutDWG, 0.0, 50.0);
        const bg::Vec3 minus = bg::targetWhiteRGB(bg::kGamutDWG, 0.0, -50.0);
        const double plusGreen = plus.v[1] / (0.5 * (plus.v[0] + plus.v[2]));
        const double minusGreen = minus.v[1] / (0.5 * (minus.v[0] + minus.v[2]));
        check(plusGreen < minusGreen,
              "positive Tint should move toward magenta and negative Tint toward green");
    }

    if (failures) {
        std::printf("%d white-balance test(s) failed\n", failures);
        return 1;
    }

    std::printf("all white-balance regression tests passed\n");
    return 0;
}
