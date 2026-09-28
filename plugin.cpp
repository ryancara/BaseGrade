/*
 *  BaseGrade OpenFX plugin.
 *
 *  Tone Equalizer core derived from ART. GPL-3.0-or-later; see teq_core.h.
 *
 *  BaseGrade decodes the selected input transfer to scene-linear RGB while
 *  keeping the selected input gamut unchanged. Primary controls and the Tone
 *  Equalizer operate in that linear gamut, then the result is encoded back to
 *  the selected input transfer.
 */
#include "ofxsImageEffect.h"
#include "teq_core.h"
#include "color_management.h"
#include "exposure.h"
#include "white_balance.h"
#include "regularization.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#define kPluginName "BaseGrade"
#define kPluginGrouping "Color"
#define kPluginDescription                                                     \
    "Photo-oriented primary grading controls for OpenFX. The current build "   \
    "contains scene-linear Exposure, selectable Temp/Tint white balance, and " \
    "an ART-derived spatial Tone Equalizer with explicit input gamut and "      \
    "transfer handling."
#define kPluginIdentifier "io.github.ryancara.BaseGrade"
#define kPluginVersionMajor 0
#define kPluginVersionMinor 5

namespace {

const char *kBandNames[5] = {"blacks", "shadows", "midtones", "highlights", "whites"};
const char *kBandLabels[5] = {"Blacks", "Shadows", "Midtones", "Highlights", "Whites"};

class BaseGrade : public OFX::ImageEffect {
public:
    explicit BaseGrade(OfxImageEffectHandle h) : OFX::ImageEffect(h)
    {
        dst_ = fetchClip(kOfxImageEffectOutputClipName);
        src_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        gamut_ = fetchChoiceParam("inputGamut");
        transfer_ = fetchChoiceParam("inputTransfer");
        exposure_ = fetchDoubleParam("exposure");
        wbMethod_ = fetchChoiceParam("whiteBalanceMethod");
        temperature_ = fetchDoubleParam("temperature");
        tint_ = fetchDoubleParam("tint");
        for (int i = 0; i < 5; ++i) bands_[i] = fetchIntParam(kBandNames[i]);
        pivot_ = fetchDoubleParam("pivot");
        // Keep the internal parameter ID stable for BaseGrade project/preset
        // compatibility going forward, while exposing ART's UI name.
        regularization_ = fetchIntParam("detail");
        regularizationScale_ = fetchDoubleParam("regularizationScale");
        showMap_ = fetchBooleanParam("showMap");
    }

    void render(const OFX::RenderArguments &args) override
    {
        std::unique_ptr<OFX::Image> src(src_->fetchImage(args.time));
        std::unique_ptr<OFX::Image> dst(dst_->fetchImage(args.time));
        if (!src || !dst) OFX::throwSuiteStatusException(kOfxStatFailed);
        if (src->getPixelDepth() != OFX::eBitDepthFloat ||
            dst->getPixelDepth() != OFX::eBitDepthFloat ||
            src->getPixelComponents() != OFX::ePixelComponentRGBA ||
            dst->getPixelComponents() != OFX::ePixelComponentRGBA)
            OFX::throwSuiteStatusException(kOfxStatErrFormat);

        int gamutIndex = bg::kGamutDWG;
        int transferIndex = bg::kTransferDaVinciIntermediate;
        gamut_->getValueAtTime(args.time, gamutIndex);
        transfer_->getValueAtTime(args.time, transferIndex);
        gamutIndex = std::max(0, std::min(gamutIndex, int(bg::kGamutCount) - 1));
        transferIndex = std::max(0, std::min(transferIndex, int(bg::kTransferCount) - 1));
        const float *lw = bg::lumaWeights(gamutIndex);

        const double exposureEV = exposure_->getValueAtTime(args.time);
        const float exposureGain = bg::exposureGain(exposureEV);

        int wbMethod = bg::kWhiteBalanceLinearRGB;
        wbMethod_->getValueAtTime(args.time, wbMethod);
        wbMethod = std::max(0, std::min(wbMethod, int(bg::kWhiteBalanceMethodCount) - 1));
        const double temperature = temperature_->getValueAtTime(args.time);
        const double tint = tint_->getValueAtTime(args.time);
        const bg::Mat3 wb = bg::makeWhiteBalanceTransform(
            gamutIndex, wbMethod, temperature, tint);

        teq::Params pp;
        for (int i = 0; i < 5; ++i) pp.bands[i] = bands_[i]->getValueAtTime(args.time);
        pp.pivot = pivot_->getValueAtTime(args.time);
        pp.regularization = regularization_->getValueAtTime(args.time);
        const double regularizationScale = regularizationScale_->getValueAtTime(args.time);
        const bool showMap = showMap_->getValueAtTime(args.time);

        teq::ToneEqualizer eq(pp);
        const float pivotGain = teq::ToneEqualizer::pivotGain(pp.pivot);

        const OfxRectI sb = src->getBounds();
        const int sw = sb.x2 - sb.x1, sh = sb.y2 - sb.y1;
        std::vector<const float *> srcRows(sh);
        for (int y = 0; y < sh; ++y)
            srcRows[y] = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + y));

        // Build the spatial mask from scene-linear RGB in the selected gamut.
        // Exposure and white balance are upstream of Tone EQ, so changing them
        // naturally moves image content through the equalizer's tonal bands.
        teq::Plane Y(sw, sh);
        teq::parallelRange(sh, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float *p = srcRows[y];
                float *yr = Y.row(y);
                for (int x = 0; x < sw; ++x) {
                    const float er = bg::applyExposure(
                        bg::decodeTransfer(p[4 * x], transferIndex), exposureGain);
                    const float eg = bg::applyExposure(
                        bg::decodeTransfer(p[4 * x + 1], transferIndex), exposureGain);
                    const float eb = bg::applyExposure(
                        bg::decodeTransfer(p[4 * x + 2], transferIndex), exposureGain);
                    float r, g, b;
                    bg::applyWhiteBalance(wb, er, eg, eb, r, g, b);
                    float l = (lw[0] * r + lw[1] * g + lw[2] * b) * pivotGain;
                    if (!(l > 1e-5f)) l = 1e-5f; // also catches NaN/negative mask luma
                    yr[x] = l > 32.f ? 32.f : l;
                }
            }
        });

        // 1.0x takes ART's exact original path. Other values change only the
        // large 350 px regularization stage; the small ~5 px conditioning pass
        // remains ART-compatible.
        bg::filterMaskScaled(eq, pp, Y, args.renderScale.x, regularizationScale);

        const OfxRectI rw = args.renderWindow;
        const int rh = rw.y2 - rw.y1;
        std::vector<float *> dstRows(rh);
        for (int y = 0; y < rh; ++y)
            dstRows[y] = static_cast<float *>(dst->getPixelAddress(rw.x1, rw.y1 + y));

        teq::parallelRange(rh, [&](int y0, int y1) {
            for (int j = y0; j < y1; ++j) {
                const int y = rw.y1 + j;
                float *d = dstRows[j];
                const int sy = y - sb.y1;
                const bool rowOk = sy >= 0 && sy < sh;
                for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
                    const int sx = x - sb.x1;
                    if (!rowOk || sx < 0 || sx >= sw) {
                        d[0] = d[1] = d[2] = d[3] = 0.f;
                        continue;
                    }
                    const float *s = srcRows[sy] + 4 * sx;
                    const float ym = Y.row(sy)[sx];
                    if (showMap) {
                        float map[3];
                        eq.color(ym, map);
                        d[0] = bg::encodeTransfer(map[0], transferIndex);
                        d[1] = bg::encodeTransfer(map[1], transferIndex);
                        d[2] = bg::encodeTransfer(map[2], transferIndex);
                    } else {
                        const float c = eq.correction(ym);
                        const float er = bg::applyExposure(
                            bg::decodeTransfer(s[0], transferIndex), exposureGain);
                        const float eg = bg::applyExposure(
                            bg::decodeTransfer(s[1], transferIndex), exposureGain);
                        const float eb = bg::applyExposure(
                            bg::decodeTransfer(s[2], transferIndex), exposureGain);
                        float r, g, b;
                        bg::applyWhiteBalance(wb, er, eg, eb, r, g, b);
                        d[0] = bg::encodeTransfer(r * c, transferIndex);
                        d[1] = bg::encodeTransfer(g * c, transferIndex);
                        d[2] = bg::encodeTransfer(b * c, transferIndex);
                    }
                    d[3] = s[3];
                }
            }
        });
    }

private:
    OFX::Clip *dst_ = nullptr, *src_ = nullptr;
    OFX::ChoiceParam *gamut_ = nullptr;
    OFX::ChoiceParam *transfer_ = nullptr;
    OFX::DoubleParam *exposure_ = nullptr;
    OFX::ChoiceParam *wbMethod_ = nullptr;
    OFX::DoubleParam *temperature_ = nullptr;
    OFX::DoubleParam *tint_ = nullptr;
    OFX::IntParam *bands_[5] = {};
    OFX::DoubleParam *pivot_ = nullptr;
    OFX::IntParam *regularization_ = nullptr;
    OFX::DoubleParam *regularizationScale_ = nullptr;
    OFX::BooleanParam *showMap_ = nullptr;
};

mDeclarePluginFactory(BaseGradeFactory, {}, {});

void BaseGradeFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false); // regularisation needs the whole frame
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);
}

void BaseGradeFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                         OFX::ContextEnum /*context*/)
{
    OFX::ClipDescriptor *sc = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    sc->addSupportedComponent(OFX::ePixelComponentRGBA);
    sc->setTemporalClipAccess(false);
    sc->setSupportsTiles(false);
    sc->setIsMask(false);

    OFX::ClipDescriptor *dc = desc.defineClip(kOfxImageEffectOutputClipName);
    dc->addSupportedComponent(OFX::ePixelComponentRGBA);
    dc->setSupportsTiles(false);

    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::ChoiceParamDescriptor *p = desc.defineChoiceParam("inputGamut");
        p->setLabels("Input Gamut", "Input Gamut", "Input Gamut");
        p->appendOption("DaVinci Wide Gamut");
        p->appendOption("Rec.709 / sRGB");
        p->appendOption("Rec.2020");
        p->appendOption("ACEScg (AP1)");
        p->appendOption("ACES2065-1 (AP0)");
        p->appendOption("Adobe RGB");
        p->appendOption("ProPhoto RGB");
        p->appendOption("ART sRGB (D50 parity)");
        p->appendOption("ART Adobe RGB (D50 parity)");
        p->appendOption("ART Rec2020 (D50 parity)");
        p->appendOption("ART ACESp0 (D50 parity)");
        p->appendOption("ART ACESp1 (D50 parity)");
        p->setDefault(bg::kGamutDWG);
        p->setHint("Selects the linear RGB gamut used for luminance and white "
                   "balance calculations. BaseGrade does not convert the gamut internally.");
        page->addChild(*p);
    }
    {
        OFX::ChoiceParamDescriptor *p = desc.defineChoiceParam("inputTransfer");
        p->setLabels("Input Transfer", "Input Transfer", "Input Transfer");
        p->appendOption("DaVinci Intermediate");
        p->appendOption("Linear");
        p->setDefault(bg::kTransferDaVinciIntermediate);
        p->setHint("Transfer function of the incoming RGB values. BaseGrade "
                   "decodes to scene-linear for processing and re-encodes afterwards.");
        page->addChild(*p);
    }
    {
        OFX::DoubleParamDescriptor *p = desc.defineDoubleParam("exposure");
        p->setLabels("Exposure (EV)", "Exposure (EV)", "Exposure (EV)");
        p->setDoubleType(OFX::eDoubleTypePlain);
        p->setDefault(0.0);
        p->setRange(-10.0, 10.0);
        p->setDisplayRange(-5.0, 5.0);
        p->setHint("Scene-linear photographic exposure applied before the Tone "
                   "Equalizer. +1 EV doubles linear RGB; -1 EV halves it.");
        p->setAnimates(true);
        page->addChild(*p);
    }
    {
        OFX::ChoiceParamDescriptor *p = desc.defineChoiceParam("whiteBalanceMethod");
        p->setLabels("White Balance Method", "White Balance Method", "White Balance Method");
        p->appendOption("Linear RGB Gain");
        p->appendOption("Bradford");
        p->setDefault(bg::kWhiteBalanceLinearRGB);
        p->setHint("Choose how the same colourimetric Temperature/Tint target "
                   "white is applied. Linear RGB Gain scales the selected working "
                   "space primaries directly; Bradford adapts in a cone-response basis.");
        page->addChild(*p);
    }
    {
        OFX::DoubleParamDescriptor *p = desc.defineDoubleParam("temperature");
        p->setLabels("Temperature", "Temperature", "Temperature");
        p->setDoubleType(OFX::eDoubleTypePlain);
        p->setDefault(0.0);
        p->setRange(-115.0, 450.0);
        // Keep the normal grading range/feel unchanged. Resolve can still accept
        // typed values outside this display range up to the hard parameter range.
        p->setDisplayRange(-100.0, 100.0);
        p->setHint("Relative reciprocal-colour-temperature shift around the "
                   "selected gamut's reference white. Positive warms, negative "
                   "cools; one unit always equals one mired. Extended values "
                   "outside +/-100 are available for extreme corrections.");
        p->setAnimates(true);
        page->addChild(*p);
    }
    {
        OFX::DoubleParamDescriptor *p = desc.defineDoubleParam("tint");
        p->setLabels("Tint", "Tint", "Tint");
        p->setDoubleType(OFX::eDoubleTypePlain);
        p->setDefault(0.0);
        p->setRange(-200.0, 200.0);
        // Preserve the original slider sensitivity for normal adjustments.
        p->setDisplayRange(-100.0, 100.0);
        p->setHint("Colourimetric green/magenta shift perpendicular to the "
                   "Planckian locus in CIE 1960 u,v. Positive is magenta; "
                   "negative is green. Extended values outside +/-100 are "
                   "available for extreme corrections.");
        p->setAnimates(true);
        page->addChild(*p);
    }

    for (int i = 0; i < 5; ++i) {
        OFX::IntParamDescriptor *p = desc.defineIntParam(kBandNames[i]);
        p->setLabels(kBandLabels[i], kBandLabels[i], kBandLabels[i]);
        p->setDefault(0);
        p->setRange(-100, 100);
        p->setDisplayRange(-100, 100);
        p->setHint("ART-compatible adjustment for this tonal range (-100..100).");
        p->setAnimates(true);
        page->addChild(*p);
    }
    {
        OFX::DoubleParamDescriptor *p = desc.defineDoubleParam("pivot");
        p->setLabels("Pivot (EV)", "Pivot (EV)", "Pivot (EV)");
        p->setDoubleType(OFX::eDoubleTypePlain);
        p->setDefault(0.0);
        p->setRange(-12.0, 12.0);
        p->setDisplayRange(-12.0, 12.0);
        p->setHint("Slides the Tone Equalizer's tonal bands along the exposure range.");
        page->addChild(*p);
    }
    {
        // Retain the original internal ID "detail" for BaseGrade project and
        // preset compatibility going forward, while exposing ART's UI name.
        OFX::IntParamDescriptor *p = desc.defineIntParam("detail");
        p->setLabels("Regularization", "Regularization", "Regularization");
        p->setDefault(4);
        p->setRange(0, 4);
        p->setDisplayRange(0, 4);
        p->setHint("ART-compatible local-contrast preservation. 0 follows raw "
                   "pixel luminance most closely; 4 is most regularized.");
        page->addChild(*p);
    }
    {
        OFX::DoubleParamDescriptor *p = desc.defineDoubleParam("regularizationScale");
        p->setLabels("Regularization Scale", "Regularization Scale", "Regularization Scale");
        p->setDoubleType(OFX::eDoubleTypeScale);
        p->setDefault(1.0);
        p->setRange(0.05, 8.0);
        p->setDisplayRange(0.25, 4.0);
        p->setHint("Spatial scale of ART's large regularization pass. 1.0x is "
                   "the original 350-pixel full-resolution radius; 0.5x is "
                   "about 175 px and 2.0x about 700 px. This control affects "
                   "Regularization levels 2-4 only.");
        page->addChild(*p);
    }
    {
        OFX::BooleanParamDescriptor *p = desc.defineBooleanParam("showMap");
        p->setLabels("Show colour map", "Show colour map", "Show colour map");
        p->setDefault(false);
        p->setHint("Diagnostic tonal map: purple=blacks, blue=shadows, "
                   "grey=midtones, yellow=highlights, red=whites.");
        page->addChild(*p);
    }
}

OFX::ImageEffect *BaseGradeFactory::createInstance(OfxImageEffectHandle handle,
                                                   OFX::ContextEnum)
{
    return new BaseGrade(handle);
}

} // namespace

namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static BaseGradeFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
} // namespace Plugin
} // namespace OFX
