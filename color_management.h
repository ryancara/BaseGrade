#pragma once

#include <algorithm>
#include <cmath>

namespace bg {

enum GamutIndex {
    kGamutDWG = 0,
    kGamutRec709,
    kGamutRec2020,
    kGamutACEScg,
    kGamutACES2065,
    kGamutAdobeRGB,
    kGamutProPhoto,
    kGamutARTsRGB,
    kGamutARTAdobeRGB,
    kGamutARTRec2020,
    kGamutARTACESp0,
    kGamutARTACESp1,
    kGamutCount
};

enum TransferIndex {
    kTransferDaVinciIntermediate = 0,
    kTransferLinear,
    kTransferCount
};

// RGB->XYZ Y rows for each linear gamut.
// DWG is from Blackmagic Design's published RGB->XYZ matrix.
// ART entries are the exact D50-adapted working-space rows ART uses to build
// its Tone Equalizer luminance mask.
inline constexpr float kLuma[kGamutCount][3] = {
    {0.27411851f, 0.87363190f, -0.14775041f}, // DaVinci Wide Gamut (D65)
    {0.21263901f, 0.71516868f,  0.07219232f}, // Rec.709 / sRGB (D65)
    {0.26270021f, 0.67799807f,  0.05930172f}, // Rec.2020 (D65)
    {0.27222872f, 0.67408177f,  0.05368952f}, // ACEScg / AP1 (D60)
    {0.34396645f, 0.72816610f, -0.07213255f}, // ACES2065-1 / AP0 (D60)
    {0.29734498f, 0.62736357f,  0.07529146f}, // Adobe RGB (D65)
    {0.28803745f, 0.71187688f,  0.00008566f}, // ProPhoto RGB (D50)
    {0.2225045f,  0.7168786f,   0.0606169f},  // ART sRGB working space
    {0.3111242f,  0.6256560f,   0.0632197f},  // ART Adobe RGB
    {0.2790177f,  0.6753402f,   0.0456377f},  // ART Rec2020
    {0.3618807f,  0.72255045f, -0.0843859f},  // ART ACESp0
    {0.2844480f,  0.6717580f,   0.0437940f}   // ART ACESp1
};

// Blackmagic Design DaVinci Intermediate constants.
inline constexpr float DI_A = 0.0075f;
inline constexpr float DI_B = 7.0f;
inline constexpr float DI_C = 0.07329248f;
inline constexpr float DI_M = 10.44426855f;
inline constexpr float DI_LIN_CUT = 0.00262409f;
inline constexpr float DI_LOG_CUT = 0.02740668f;

inline float decodeTransfer(float v, int transfer)
{
    if (transfer == kTransferDaVinciIntermediate) {
        return v > DI_LOG_CUT
            ? std::pow(2.0f, (v / DI_C) - DI_B) - DI_A
            : v / DI_M;
    }
    return v;
}

inline float encodeTransfer(float l, int transfer)
{
    if (transfer == kTransferDaVinciIntermediate) {
        return l > DI_LIN_CUT
            ? (std::log2(l + DI_A) + DI_B) * DI_C
            : l * DI_M;
    }
    return l;
}

inline const float *lumaWeights(int gamut)
{
    gamut = std::max(0, std::min(gamut, int(kGamutCount) - 1));
    return kLuma[gamut];
}

} // namespace bg
