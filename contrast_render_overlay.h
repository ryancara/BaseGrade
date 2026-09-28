#pragma once

#include "contrast.h"
#include "color_management.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bg {

// DCTL-style diagnostic overlay: rasterises the current scalar contrast curve
// directly into the rendered image when Show Curve is enabled. It deliberately
// does not use the OFX viewer-overlay API, which is inconsistently exposed by
// some Resolve pages/hosts.
class ContrastCurveRasterOverlay {
public:
    ContrastCurveRasterOverlay(int imageWidth, int imageHeight,
                               int transferIndex, const ContrastParams &params)
        : transferIndex_(transferIndex), pivotEV_(params.pivotEV),
          prepared_(prepareContrast(params))
    {
        if (imageWidth < 64 || imageHeight < 64) return;

        marginX_ = std::max(8, imageWidth / 40);
        marginY_ = std::max(8, imageHeight / 40);
        width_ = std::max(160, int(std::lround(imageWidth * 0.30)));
        height_ = std::max(120, int(std::lround(imageHeight * 0.26)));
        width_ = std::min(width_, imageWidth - 2 * marginX_);
        height_ = std::min(height_, imageHeight - 2 * marginY_);
        if (width_ < 32 || height_ < 32) return;

        line_ = std::max(1, std::min(imageWidth, imageHeight) / 900);
        pointRadius_ = std::max(3, line_ * 3);

        curveY_.resize(width_);
        for (int x = 0; x < width_; ++x) {
            const double t = width_ > 1 ? double(x) / double(width_ - 1) : 0.0;
            const double inputEV = kMinEV + t * (kMaxEV - kMinEV);
            curveY_[x] = evToY(outputEV(inputEV));
        }

        zeroX_ = evToX(0.0);
        zeroY_ = evToY(0.0);
        pivotX_ = evToX(pivotEV_);
        pivotY_ = evToY(outputEV(pivotEV_));
        valid_ = true;
    }

    bool valid() const { return valid_; }

    void composite(int localX, int localY, float &r, float &g, float &b) const
    {
        if (!valid_) return;
        const int x = localX - marginX_;
        const int y = localY - marginY_;
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return;

        const float black = encodeTransfer(0.005f, transferIndex_);
        const float mid = encodeTransfer(0.18f, transferIndex_);
        const float white = encodeTransfer(1.0f, transferIndex_);

        // Semi-transparent dark panel first.
        blend(r, g, b, black, black, black, 0.68f);

        const bool border = x < line_ || y < line_ ||
                            x >= width_ - line_ || y >= height_ - line_;
        if (border) {
            blend(r, g, b, mid, mid, mid, 0.75f);
            return;
        }

        // Middle-grey crosshair.
        if (std::abs(x - zeroX_) <= line_ || std::abs(y - zeroY_) <= line_)
            blend(r, g, b, mid, mid, mid, 0.28f);

        // Identity diagonal in the same -8..+6 EV graph domain.
        const int identityY = int(std::lround(
            double(x) * double(height_ - 1) / double(std::max(1, width_ - 1))));
        if (std::abs(y - identityY) <= line_)
            blend(r, g, b, white, white, white, 0.32f);

        // Actual scalar contrast/toe curve. Colour Preserve is deliberately
        // not represented because its effective blend is colour-dependent.
        if (x >= 0 && x < int(curveY_.size()) &&
            std::abs(y - curveY_[x]) <= line_)
            blend(r, g, b, white, white, white, 0.98f);

        // Contrast-pivot marker follows the true scalar output. If the pivot is
        // inside the active toe region, the independent toe stage can move it.
        const int dx = x - pivotX_;
        const int dy = y - pivotY_;
        if (dx * dx + dy * dy <= pointRadius_ * pointRadius_)
            blend(r, g, b, white, white, white, 1.0f);
    }

private:
    static constexpr double kMinEV = -8.0;
    static constexpr double kMaxEV = 6.0;

    double outputEV(double inputEV) const
    {
        const float in = float(0.18 * std::pow(2.0, inputEV));
        const float out = applyContrastScalar(in, prepared_);
        if (!(out > 0.0f) || !std::isfinite(out)) return kMinEV;
        return std::max(kMinEV,
                        std::min(std::log2(double(out) / 0.18), kMaxEV));
    }

    int evToX(double ev) const
    {
        const double t = (std::max(kMinEV, std::min(ev, kMaxEV)) - kMinEV) /
                         (kMaxEV - kMinEV);
        return int(std::lround(t * double(width_ - 1)));
    }

    int evToY(double ev) const
    {
        const double t = (std::max(kMinEV, std::min(ev, kMaxEV)) - kMinEV) /
                         (kMaxEV - kMinEV);
        return int(std::lround(t * double(height_ - 1)));
    }

    static void blend(float &r, float &g, float &b,
                      float or_, float og, float ob, float alpha)
    {
        const float inv = 1.0f - alpha;
        r = inv * r + alpha * or_;
        g = inv * g + alpha * og;
        b = inv * b + alpha * ob;
    }

    int transferIndex_ = kTransferDaVinciIntermediate;
    double pivotEV_ = 0.0;
    PreparedContrast prepared_;
    int marginX_ = 0, marginY_ = 0;
    int width_ = 0, height_ = 0;
    int line_ = 1, pointRadius_ = 3;
    int zeroX_ = 0, zeroY_ = 0;
    int pivotX_ = 0, pivotY_ = 0;
    std::vector<int> curveY_;
    bool valid_ = false;
};

} // namespace bg
