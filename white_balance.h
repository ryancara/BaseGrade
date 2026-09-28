#pragma once

#include "color_management.h"

#include <algorithm>
#include <cmath>

namespace bg {

enum WhiteBalanceMethod {
    kWhiteBalanceLinearRGB = 0,
    kWhiteBalanceBradford,
    kWhiteBalanceMethodCount
};

struct Vec3 {
    double v[3] = {0.0, 0.0, 0.0};
};

struct Mat3 {
    double m[3][3] = {{1.0, 0.0, 0.0},
                      {0.0, 1.0, 0.0},
                      {0.0, 0.0, 1.0}};
};

inline Vec3 mul(const Mat3 &a, const Vec3 &x)
{
    Vec3 r;
    for (int i = 0; i < 3; ++i) {
        r.v[i] = a.m[i][0] * x.v[0] + a.m[i][1] * x.v[1] +
                 a.m[i][2] * x.v[2];
    }
    return r;
}

inline Mat3 mul(const Mat3 &a, const Mat3 &b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = 0.0;
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    }
    return r;
}

inline Mat3 diagonal(const Vec3 &d)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = i == j ? d.v[i] : 0.0;
    return r;
}

inline Mat3 inverse(const Mat3 &a)
{
    const double a00 = a.m[0][0], a01 = a.m[0][1], a02 = a.m[0][2];
    const double a10 = a.m[1][0], a11 = a.m[1][1], a12 = a.m[1][2];
    const double a20 = a.m[2][0], a21 = a.m[2][1], a22 = a.m[2][2];

    const double c00 = a11 * a22 - a12 * a21;
    const double c01 = a02 * a21 - a01 * a22;
    const double c02 = a01 * a12 - a02 * a11;
    const double c10 = a12 * a20 - a10 * a22;
    const double c11 = a00 * a22 - a02 * a20;
    const double c12 = a02 * a10 - a00 * a12;
    const double c20 = a10 * a21 - a11 * a20;
    const double c21 = a01 * a20 - a00 * a21;
    const double c22 = a00 * a11 - a01 * a10;

    const double det = a00 * c00 + a01 * c10 + a02 * c20;
    const double invDet = 1.0 / det;

    Mat3 r{};
    r.m[0][0] = c00 * invDet; r.m[0][1] = c01 * invDet; r.m[0][2] = c02 * invDet;
    r.m[1][0] = c10 * invDet; r.m[1][1] = c11 * invDet; r.m[1][2] = c12 * invDet;
    r.m[2][0] = c20 * invDet; r.m[2][1] = c21 * invDet; r.m[2][2] = c22 * invDet;
    return r;
}

inline constexpr double kRGBtoXYZ[kGamutCount][3][3] = {
    { // DaVinci Wide Gamut, D65
        {0.700622392, 0.148774815, 0.101058720},
        {0.274118511, 0.873631896, -0.147750407},
        {-0.098962913, -0.137895325, 1.325915989}},
    { // Rec.709 / sRGB, D65
        {0.412390799, 0.357584339, 0.180480788},
        {0.212639006, 0.715168679, 0.072192315},
        {0.019330819, 0.119194780, 0.950532152}},
    { // Rec.2020, D65
        {0.636958048, 0.144616904, 0.168880975},
        {0.262700212, 0.677998072, 0.059301716},
        {0.000000000, 0.028072693, 1.060985058}},
    { // ACEScg / AP1, D60
        {0.662454181, 0.134004206, 0.156187687},
        {0.272228717, 0.674081766, 0.053689517},
        {-0.005574649, 0.004060734, 1.010339100}},
    { // ACES2065-1 / AP0, D60
        {0.952552396, 0.000000000, 0.000093679},
        {0.343966450, 0.728166097, -0.072132546},
        {0.000000000, 0.000000000, 1.008825184}},
    { // Adobe RGB, D65
        {0.576669043, 0.185558238, 0.188228646},
        {0.297344975, 0.627363566, 0.075291458},
        {0.027031361, 0.070688853, 0.991337537}},
    { // ProPhoto RGB, D50
        {0.797667235, 0.135192231, 0.031352529},
        {0.288037454, 0.711876883, 0.000085663},
        {0.000000000, 0.000000000, 0.825188285}},
    { // ART sRGB, Bradford-adapted to D50
        {0.4360747, 0.3850649, 0.1430804},
        {0.2225045, 0.7168786, 0.0606169},
        {0.0139322, 0.0971045, 0.7141733}},
    { // ART Adobe RGB, Bradford-adapted to D50
        {0.6097559, 0.2052401, 0.1492240},
        {0.3111242, 0.6256560, 0.0632197},
        {0.0194811, 0.0608902, 0.7448387}},
    { // ART Rec.2020, Bradford-adapted to D50
        {0.6734241, 0.1656411, 0.1251286},
        {0.2790177, 0.6753402, 0.0456377},
        {-0.0019300, 0.0299784, 0.7973330}},
    { // ART ACES AP0, Bradford-adapted to D50
        {0.9908526, 0.0122334, -0.0388654},
        {0.3618807, 0.72255045, -0.0843859},
        {-0.0027093, 0.0082323, 0.8196880}},
    { // ART ACES AP1, Bradford-adapted to D50
        {0.689697, 0.149944, 0.124559},
        {0.284448, 0.671758, 0.043794},
        {-0.006043, 0.009998, 0.820945}}
};

struct WhitePoint {
    double x;
    double y;
    double cct;
};

// CCT is used only to establish the neutral point on the reciprocal-temperature
// axis. The actual zero-control chromaticity comes from each RGB->XYZ matrix,
// ensuring exact neutrality even for rounded ART matrix constants.
inline constexpr double kReferenceCCT[kGamutCount] = {
    6504.0, 6504.0, 6504.0, 6000.0, 6000.0, 6504.0,
    5003.0, 5003.0, 5003.0, 5003.0, 5003.0, 5003.0
};

inline Mat3 rgbToXYZMatrix(int gamut)
{
    gamut = std::max(0, std::min(gamut, int(kGamutCount) - 1));
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = kRGBtoXYZ[gamut][i][j];
    return r;
}

inline Vec3 referenceWhiteXYZ(int gamut)
{
    const Mat3 rgbToXyz = rgbToXYZMatrix(gamut);
    return mul(rgbToXyz, Vec3{{1.0, 1.0, 1.0}});
}

inline WhitePoint xyzToWhitePoint(const Vec3 &xyz, double cct)
{
    const double sum = xyz.v[0] + xyz.v[1] + xyz.v[2];
    return {xyz.v[0] / sum, xyz.v[1] / sum, cct};
}

inline Vec3 xyToXYZ(double x, double y, double Y = 1.0)
{
    return {{x / y * Y, Y, (1.0 - x - y) / y * Y}};
}

struct UV {
    double u;
    double v;
};

inline UV xyToUV(double x, double y)
{
    const double d = -2.0 * x + 12.0 * y + 3.0;
    return {4.0 * x / d, 6.0 * y / d};
}

inline WhitePoint uvToWhitePoint(double u, double v, double cct)
{
    const double d = 2.0 * u - 8.0 * v + 4.0;
    return {3.0 * u / d, 2.0 * v / d, cct};
}

// Kim et al. approximation to the CIE 1931 Planckian locus, valid over
// 1667-25000 K. Temperature is intentionally handled in reciprocal-temperature
// (mired) space because equal mired shifts move much more uniformly along the
// locus than equal Kelvin shifts.
inline WhitePoint planckianWhite(double kelvin)
{
    const double T = std::max(1667.0, std::min(kelvin, 25000.0));
    double x;
    if (T <= 4000.0) {
        x = -0.2661239e9 / (T * T * T) - 0.2343589e6 / (T * T) +
            0.8776956e3 / T + 0.179910;
    } else {
        x = -3.0258469e9 / (T * T * T) + 2.1070379e6 / (T * T) +
            0.2226347e3 / T + 0.240390;
    }

    double y;
    if (T <= 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x +
            2.18555832 * x - 0.20219683;
    } else if (T <= 4000.0) {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x +
            2.09137015 * x - 0.16748867;
    } else {
        y = 3.0817580 * x * x * x - 5.87338670 * x * x +
            3.75112997 * x - 0.37001483;
    }
    return {x, y, T};
}

// Temperature is a relative mired shift: + values warm, - values cool.
// Tint is mapped to a perpendicular displacement in CIE 1960 UCS. Positive
// Tint moves toward magenta; 100 units correspond to roughly 0.02 uv.
inline WhitePoint targetWhitePoint(int gamut, double temperature, double tint)
{
    gamut = std::max(0, std::min(gamut, int(kGamutCount) - 1));
    const double referenceCCT = kReferenceCCT[gamut];
    const Vec3 sourceXYZ = referenceWhiteXYZ(gamut);
    const WhitePoint ref = xyzToWhitePoint(sourceXYZ, referenceCCT);

    double mired = 1.0e6 / referenceCCT + temperature;
    mired = std::max(40.0, std::min(mired, 600.0));
    const double targetCCT = std::max(1667.0, std::min(1.0e6 / mired, 25000.0));

    const UV refUV = xyToUV(ref.x, ref.y);
    const WhitePoint locusRefXY = planckianWhite(referenceCCT);
    const WhitePoint locusTargetXY = planckianWhite(targetCCT);
    const UV locusRef = xyToUV(locusRefXY.x, locusRefXY.y);
    const UV locusTarget = xyToUV(locusTargetXY.x, locusTargetXY.y);

    UV target = {refUV.u + (locusTarget.u - locusRef.u),
                 refUV.v + (locusTarget.v - locusRef.v)};

    const double dt = std::max(1.0, targetCCT * 0.001);
    const WhitePoint loXY = planckianWhite(std::max(1667.0, targetCCT - dt));
    const WhitePoint hiXY = planckianWhite(std::min(25000.0, targetCCT + dt));
    const UV lo = xyToUV(loXY.x, loXY.y);
    const UV hi = xyToUV(hiXY.x, hiXY.y);
    double tu = hi.u - lo.u;
    double tv = hi.v - lo.v;
    const double len = std::sqrt(tu * tu + tv * tv);
    if (len > 0.0) {
        tu /= len;
        tv /= len;
        const double duv = tint * 0.0002;
        // (-tv, tu) points toward the magenta side of the locus for increasing
        // temperature in the CIE 1960 u,v plane.
        target.u += (-tv) * duv;
        target.v += tu * duv;
    }

    return uvToWhitePoint(target.u, target.v, targetCCT);
}

inline Vec3 targetWhiteXYZ(int gamut, double temperature, double tint)
{
    const Vec3 source = referenceWhiteXYZ(gamut);
    const WhitePoint w = targetWhitePoint(gamut, temperature, tint);
    return xyToXYZ(w.x, w.y, source.v[1]);
}

inline Vec3 targetWhiteRGB(int gamut, double temperature, double tint)
{
    const Mat3 xyzToRgb = inverse(rgbToXYZMatrix(gamut));
    return mul(xyzToRgb, targetWhiteXYZ(gamut, temperature, tint));
}

inline Mat3 makeWhiteBalanceTransform(int gamut, int method,
                                      double temperature, double tint)
{
    gamut = std::max(0, std::min(gamut, int(kGamutCount) - 1));
    method = std::max(0, std::min(method, int(kWhiteBalanceMethodCount) - 1));

    // Guarantee the neutral setting is an exact identity, with no round-trip
    // matrix noise or nominal-white mismatch.
    if (temperature == 0.0 && tint == 0.0) return Mat3{};

    const Mat3 rgbToXyz = rgbToXYZMatrix(gamut);
    const Mat3 xyzToRgb = inverse(rgbToXyz);
    const Vec3 sourceXYZ = referenceWhiteXYZ(gamut);
    const Vec3 targetXYZ = targetWhiteXYZ(gamut, temperature, tint);

    if (method == kWhiteBalanceLinearRGB) {
        const Vec3 sourceRGB = mul(xyzToRgb, sourceXYZ);
        const Vec3 targetRGB = mul(xyzToRgb, targetXYZ);
        Vec3 gains;
        for (int i = 0; i < 3; ++i) gains.v[i] = targetRGB.v[i] / sourceRGB.v[i];
        return diagonal(gains);
    }

    // Bradford chromatic adaptation: XYZ -> cone-response basis -> scale the
    // three responses according to source/target white -> XYZ.
    const Mat3 bradford = {{{0.8951, 0.2664, -0.1614},
                            {-0.7502, 1.7135, 0.0367},
                            {0.0389, -0.0685, 1.0296}}};
    const Mat3 bradfordInv = inverse(bradford);
    const Vec3 sourceCone = mul(bradford, sourceXYZ);
    const Vec3 targetCone = mul(bradford, targetXYZ);
    Vec3 coneGain;
    for (int i = 0; i < 3; ++i) coneGain.v[i] = targetCone.v[i] / sourceCone.v[i];

    const Mat3 catXYZ = mul(bradfordInv, mul(diagonal(coneGain), bradford));
    return mul(xyzToRgb, mul(catXYZ, rgbToXyz));
}

inline void applyWhiteBalance(const Mat3 &m, float r, float g, float b,
                              float &outR, float &outG, float &outB)
{
    outR = float(m.m[0][0] * r + m.m[0][1] * g + m.m[0][2] * b);
    outG = float(m.m[1][0] * r + m.m[1][1] * g + m.m[1][2] * b);
    outB = float(m.m[2][0] * r + m.m[2][1] * g + m.m[2][2] * b);
}

} // namespace bg
