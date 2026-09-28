#pragma once

#include "contrast.h"
#include "ofxsImageEffect.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace bg {

class ContrastCurveOverlay : public OFX::OverlayInteract {
public:
    ContrastCurveOverlay(OfxInteractHandle handle, OFX::ImageEffect *effect)
        : OFX::OverlayInteract(handle), effect_(effect)
    {
        source_ = effect_->fetchClip(kOfxImageEffectSimpleSourceClipName);
        showCurve_ = effect_->fetchBooleanParam("showCurve");
        contrast_ = effect_->fetchDoubleParam("contrast");
        pivot_ = effect_->fetchDoubleParam("contrastPivot");
        softness_ = effect_->fetchDoubleParam("curveSoftness");
        toeAmount_ = effect_->fetchDoubleParam("toeStrength");
        toeRange_ = effect_->fetchDoubleParam("toeRange");

        addParamToSlaveTo(showCurve_);
        addParamToSlaveTo(contrast_);
        addParamToSlaveTo(pivot_);
        addParamToSlaveTo(softness_);
        addParamToSlaveTo(toeAmount_);
        addParamToSlaveTo(toeRange_);
    }

    bool draw(const OFX::DrawArgs &args) override
    {
        if (!showCurve_->getValueAtTime(args.time)) return false;

        const double px = std::fabs(args.pixelScale.x);
        const double py = std::fabs(args.pixelScale.y);
        if (!(px > 0.0) || !(py > 0.0)) return false;

        const OfxRectD rod = source_->getRegionOfDefinition(args.time);

        // Keep the overlay approximately constant in screen pixels while
        // anchoring it near the lower-left of the source image.
        const double marginX = 24.0 * px;
        const double marginY = 24.0 * py;
        const double width = 260.0 * px;
        const double height = 180.0 * py;
        const double x0 = rod.x1 + marginX;
        const double y0 = rod.y1 + marginY;
        const double x1 = x0 + width;
        const double y1 = y0 + height;

        constexpr double minEV = -8.0;
        constexpr double maxEV = 6.0;
        constexpr int samples = 192;

        ContrastParams p;
        p.contrast = contrast_->getValueAtTime(args.time);
        p.pivotEV = pivot_->getValueAtTime(args.time);
        p.softness = softness_->getValueAtTime(args.time);
        p.toeStrength = toeAmount_->getValueAtTime(args.time);
        p.toeRangeEV = toeRange_->getValueAtTime(args.time);

        auto graphX = [&](double ev) {
            const double t = (ev - minEV) / (maxEV - minEV);
            return x0 + std::max(0.0, std::min(t, 1.0)) * width;
        };
        auto graphY = [&](double ev) {
            const double t = (ev - minEV) / (maxEV - minEV);
            return y0 + std::max(0.0, std::min(t, 1.0)) * height;
        };
        auto outputEV = [&](double inputEV) {
            const float in = float(0.18 * std::pow(2.0, inputEV));
            const float out = applyContrastScalar(in, p);
            if (!(out > 0.0f) || !std::isfinite(out)) return minEV;
            return std::max(minEV, std::min(std::log2(double(out) / 0.18), maxEV));
        };

        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Translucent graph panel.
        glColor4f(0.03f, 0.03f, 0.03f, 0.72f);
        glBegin(GL_QUADS);
        glVertex2d(x0, y0);
        glVertex2d(x1, y0);
        glVertex2d(x1, y1);
        glVertex2d(x0, y1);
        glEnd();

        // Border and middle-grey crosshair.
        glLineWidth(1.0f);
        glColor4f(0.75f, 0.75f, 0.75f, 0.55f);
        glBegin(GL_LINE_LOOP);
        glVertex2d(x0, y0);
        glVertex2d(x1, y0);
        glVertex2d(x1, y1);
        glVertex2d(x0, y1);
        glEnd();

        const double zeroX = graphX(0.0);
        const double zeroY = graphY(0.0);
        glColor4f(0.65f, 0.65f, 0.65f, 0.20f);
        glBegin(GL_LINES);
        glVertex2d(zeroX, y0);
        glVertex2d(zeroX, y1);
        glVertex2d(x0, zeroY);
        glVertex2d(x1, zeroY);
        glEnd();

        // Identity diagonal.
        glColor4f(0.75f, 0.75f, 0.75f, 0.35f);
        glBegin(GL_LINES);
        glVertex2d(graphX(minEV), graphY(minEV));
        glVertex2d(graphX(maxEV), graphY(maxEV));
        glEnd();

        // Actual scalar tone curve. Colour Preserve is intentionally excluded:
        // it changes how this curve is applied to RGB, not the curve itself.
        glLineWidth(2.0f);
        glColor4f(1.0f, 1.0f, 1.0f, 0.95f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= samples; ++i) {
            const double t = double(i) / samples;
            const double inEV = minEV + t * (maxEV - minEV);
            glVertex2d(graphX(inEV), graphY(outputEV(inEV)));
        }
        glEnd();

        // Mark the current Contrast Pivot input position on the final curve.
        const double pivotEV = std::max(minEV, std::min(p.pivotEV, maxEV));
        glPointSize(7.0f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_POINTS);
        glVertex2d(graphX(pivotEV), graphY(outputEV(pivotEV)));
        glEnd();

        glPopAttrib();
        return true;
    }

private:
    OFX::ImageEffect *effect_ = nullptr;
    OFX::Clip *source_ = nullptr;
    OFX::BooleanParam *showCurve_ = nullptr;
    OFX::DoubleParam *contrast_ = nullptr;
    OFX::DoubleParam *pivot_ = nullptr;
    OFX::DoubleParam *softness_ = nullptr;
    OFX::DoubleParam *toeAmount_ = nullptr;
    OFX::DoubleParam *toeRange_ = nullptr;
};

class ContrastCurveOverlayDescriptor
    : public OFX::DefaultEffectOverlayDescriptor<ContrastCurveOverlayDescriptor,
                                                  ContrastCurveOverlay> {};

} // namespace bg
