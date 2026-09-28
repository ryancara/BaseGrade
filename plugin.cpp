/*
 *  OFX port of the ART (Advanced RawTherapee) tone equalizer.
 *  GPL-3.0-or-later, see teq_core.h for attribution.
 *
 *  Input must be SCENE-LINEAR float RGB(A).  In DaVinci Resolve, put a Color
 *  Space Transform node (to linear) before this and one back after it.
 */
#include "ofxsImageEffect.h"
#include "teq_core.h"

#include <algorithm>
#include <memory>
#include <vector>

#define kPluginName "ART Tone Equalizer"
#define kPluginGrouping "Color"
#define kPluginDescription                                                     \
    "Port of the tone equalizer from ART. Works on scene-linear RGB: an "      \
    "edge-aware luminance mask selects tonal ranges, and per-band gains "      \
    "(blacks/shadows/midtones/highlights/whites) are applied as a "            \
    "multiplicative correction."
#define kPluginIdentifier "org.example.ArtToneEqualizer"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

namespace {

const char *kBandNames[5] = {"blacks", "shadows", "midtones", "highlights", "whites"};
const char *kBandLabels[5] = {"Blacks", "Shadows", "Midtones", "Highlights", "Whites"};

// Exact Y rows from ART's built-in D50-adapted working-space matrices
// (rtengine/iccmatrices.h). Tone Equalizer uses workingSpaceMatrix() and
// Color::rgbLuminance(), so these coefficients matter for parity.
const float kLuma[6][3] = {
    {0.2225045f, 0.7168786f,  0.0606169f}, // sRGB (ART working space)
    {0.3111242f, 0.6256560f,  0.0632197f}, // Adobe RGB
    {0.2880402f, 0.7118741f,  0.0000857f}, // ProPhoto
    {0.2790177f, 0.6753402f,  0.0456377f}, // Rec2020 (ART default)
    {0.3618807f, 0.72255045f, -0.0843859f},// ACESp0
    {0.2844480f, 0.6717580f,  0.0437940f}  // ACESp1 / ACEScg
};

class ArtToneEq : public OFX::ImageEffect {
public:
    explicit ArtToneEq(OfxImageEffectHandle h) : OFX::ImageEffect(h)
    {
        dst_ = fetchClip(kOfxImageEffectOutputClipName);
        src_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        for (int i = 0; i < 5; ++i) bands_[i] = fetchIntParam(kBandNames[i]);
        pivot_ = fetchDoubleParam("pivot");
        detail_ = fetchIntParam("detail");
        luma_ = fetchChoiceParam("lumaWeights");
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

        teq::Params pp;
        for (int i = 0; i < 5; ++i) pp.bands[i] = bands_[i]->getValueAtTime(args.time);
        pp.pivot = pivot_->getValueAtTime(args.time);
        pp.regularization = detail_->getValueAtTime(args.time);
        const bool showMap = showMap_->getValueAtTime(args.time);
        int lumaIndex = 0;
        luma_->getValueAtTime(args.time, lumaIndex);
        lumaIndex = std::max(0, std::min(lumaIndex, 5));
        const float *lw = kLuma[lumaIndex];

        teq::ToneEqualizer eq(pp);
        const float gain = teq::ToneEqualizer::pivotGain(pp.pivot);

        // The mask is computed over the whole source image (tiles are
        // disabled in describe(), so this equals the render window).
        const OfxRectI sb = src->getBounds();
        const int sw = sb.x2 - sb.x1, sh = sb.y2 - sb.y1;
        std::vector<const float *> srcRows(sh);
        for (int y = 0; y < sh; ++y)
            srcRows[y] = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + y));

        teq::Plane Y(sw, sh);
        teq::parallelRange(sh, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float *p = srcRows[y];
                float *yr = Y.row(y);
                for (int x = 0; x < sw; ++x) {
                    float l = (lw[0] * p[4 * x] + lw[1] * p[4 * x + 1] +
                               lw[2] * p[4 * x + 2]) * gain;
                    if (!(l > 1e-5f)) l = 1e-5f; // also catches NaN
                    yr[x] = l > 32.f ? 32.f : l;
                }
            }
        });

        eq.filterMask(Y, args.renderScale.x);

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
                        eq.color(ym, d);
                    } else {
                        const float c = eq.correction(ym);
                        d[0] = s[0] * c;
                        d[1] = s[1] * c;
                        d[2] = s[2] * c;
                    }
                    d[3] = s[3];
                }
            }
        });
    }

private:
    OFX::Clip *dst_ = nullptr, *src_ = nullptr;
    OFX::IntParam *bands_[5] = {};
    OFX::DoubleParam *pivot_ = nullptr;
    OFX::IntParam *detail_ = nullptr;
    OFX::ChoiceParam *luma_ = nullptr;
    OFX::BooleanParam *showMap_ = nullptr;
};

mDeclarePluginFactory(ArtToneEqFactory, {}, {});

void ArtToneEqFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setSupportsTiles(false); // the mask needs the whole frame
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);
}

void ArtToneEqFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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

    for (int i = 0; i < 5; ++i) {
        OFX::IntParamDescriptor *p = desc.defineIntParam(kBandNames[i]);
        p->setLabels(kBandLabels[i], kBandLabels[i], kBandLabels[i]);
        p->setDefault(0);
        p->setRange(-100, 100);
        p->setDisplayRange(-100, 100);
        p->setHint("Gain for this tonal range (-100..100).");
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
        p->setHint("Exposure shift applied to the mask only; slides the tonal "
                   "bands along the histogram.");
        page->addChild(*p);
    }
    {
        OFX::IntParamDescriptor *p = desc.defineIntParam("detail");
        p->setLabels("Detail", "Detail", "Detail");
        p->setDefault(4);
        p->setRange(0, 4);
        p->setDisplayRange(0, 4);
        p->setHint("Mask regularization (ART's 'Detail'): 0 follows the image "
                   "closely, 4 is smoothest.");
        page->addChild(*p);
    }
    {
        OFX::ChoiceParamDescriptor *p = desc.defineChoiceParam("lumaWeights");
        p->setLabels("Luminance weights", "Luminance weights", "Luminance weights");
        p->appendOption("sRGB (ART)");
        p->appendOption("Adobe RGB");
        p->appendOption("ProPhoto");
        p->appendOption("Rec2020 (ART default)");
        p->appendOption("ACESp0");
        p->appendOption("ACESp1 / ACEScg");
        p->setDefault(3);
        p->setHint("ART working-space matrix used to compute luminance. For "
                   "comparison tests, choose the same working space in ART.");
        page->addChild(*p);
    }
    {
        OFX::BooleanParamDescriptor *p = desc.defineBooleanParam("showMap");
        p->setLabels("Show colour map", "Show colour map", "Show colour map");
        p->setDefault(false);
        p->setHint("Display the tonal mask as colours: purple=blacks, "
                   "blue=shadows, grey=midtones, yellow=highlights, red=whites.");
        page->addChild(*p);
    }
}

OFX::ImageEffect *ArtToneEqFactory::createInstance(OfxImageEffectHandle handle,
                                                   OFX::ContextEnum)
{
    return new ArtToneEq(handle);
}

} // namespace

namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ArtToneEqFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
} // namespace Plugin
} // namespace OFX
