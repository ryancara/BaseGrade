# BaseGrade

BaseGrade is an OpenFX image-grading plugin aimed at bringing photo-oriented primary controls to DaVinci Resolve and other OFX hosts.

The project began with a close port of ART's Tone Equalizer. The `main` branch is the known-working BaseGrade foundation; development branches add new primary controls while keeping the ART-derived core clean and independently testable.

## Current feature branch

`feature/contrast`

This branch currently includes:

- **Input Gamut** selection
- **Input Transfer** selection
- DaVinci Wide Gamut support
- native DaVinci Intermediate decode/process/re-encode
- scene-linear **Exposure**
- **Temperature** and **Tint** white-balance controls
- **White Balance Method** dropdown with `Linear RGB Gain` and `Bradford`
- ART-derived Tone Equalizer
- **Regularization** and **Regularization Scale**
- global **Contrast**, **Contrast Pivot**, **Curve Softness**, **Toe Amount**, **Toe Range**, and **Colour Preserve** controls
- raster **Show Curve** diagnostic for the global tone curve
- ART parity gamut options retained for comparison testing

No hidden gamut conversion is performed. BaseGrade decodes the selected transfer to scene-linear RGB, keeps the RGB values in the selected gamut, applies Exposure and white balance, builds the Tone Equalizer mask using that gamut's RGB-to-XYZ Y coefficients, applies the Tone Equalizer correction in linear light, applies the global contrast stage, and then re-encodes to the selected transfer.

### Exposure

Exposure is a scene-linear photographic gain applied before white balance and the Tone Equalizer:

- `+1 EV` = 2x linear RGB
- `0 EV` = identity
- `-1 EV` = 0.5x linear RGB

Because Exposure is upstream of the Tone Equalizer, changing exposure naturally moves image content through the equalizer's tonal zones.

### Temperature and Tint

Temperature and Tint are centred at zero and use the same target-white calculation for both white-balance methods.

**Temperature** is a relative reciprocal-colour-temperature shift. One control unit equals one mired across the whole range, with no accelerated tails. Positive values warm the image and negative values cool it. The zero point is anchored to the selected gamut's actual reference white, so `0` remains identity for D65, D60 and D50 working spaces.

The hard Temperature range is `-115` to `+450`, while the normal displayed slider range remains `-100` to `+100` for finer everyday control. In a D65 working space, the extended limits reach approximately the Planckian model's `25000 K` cool limit and `1667 K` warm limit.

**Tint** moves perpendicular to the Planckian locus in CIE 1960 `u,v`. Positive values move toward magenta and negative values toward green. The mapping is 0.0002 `u,v` units per control unit. The hard range is `-200` to `+200`, with the normal displayed slider range remaining `-100` to `+100`.

The two white-balance methods apply that same target white differently:

- **Linear RGB Gain** converts the target white to the selected working RGB space and applies per-channel scene-linear gains.
- **Bradford** converts through XYZ and Bradford's cone-response basis, applies the source-to-target white scaling there, then returns to the selected working RGB space.

This makes the dropdown a fair A/B test: changing the method does not change the Temperature/Tint curve itself.

Very large combined Temperature/Tint corrections can move the target white outside the selected RGB gamut and therefore produce negative scene-linear RGB channels. BaseGrade intentionally preserves those values rather than clipping or silently changing the white-balance transform. This is expected extended-range behaviour; downstream display/output transforms may clip such values.

### Global Contrast

The contrast stage is a global tone-curve operation with no spatial regularization. It is applied after the Tone Equalizer and before the selected output transfer is re-encoded. Control-dependent curve constants are prepared once per render rather than recomputed for every channel of every pixel.

- **Contrast** changes the local curve slope around its pivot. `+100` is approximately 2x local slope and `-100` approximately 0.5x.
- **Contrast Pivot (EV)** is independent of the Tone Equalizer pivot and is measured in stops relative to scene-linear 18% grey. It is the fixed point of the contrast/softness stage; if the pivot is placed inside the active toe region, the independent toe stage can move the final output at that point.
- **Curve Softness** progressively rolls off the extra contrast displacement away from the pivot. It is neutral when Contrast is zero and preserves the requested local slope at the pre-toe pivot.
- **Toe Amount** is bipolar. Positive values soften/lift deep shadows; negative values deepen/harden them.
- **Toe Range (EV)** chooses how far below 18% grey the toe begins. Larger values restrict the toe to deeper shadows.
- **Colour Preserve** blends from regular per-channel RGB contrast at `0%` toward luminance-ratio contrast at `100%`.

The luminance-ratio path is confidence-weighted rather than hard-switched. BaseGrade fades smoothly back toward the RGB result when luminance is very small or is only a small fraction of the brightest RGB channel, which protects saturated wide-gamut colours and avoids discontinuities near black. The `Y'/Y` gain is still checked for finite values and bounded as a final safety guard. Because this confidence is colour-dependent, `100%` Colour Preserve means "maximum safe luminance preservation" rather than forcing the luminance-ratio path on every pixel.

### Show Curve

**Show Curve** uses a DCTL-style raster diagnostic rather than the OFX viewer-overlay API. While enabled, the graph is composited directly into BaseGrade's output image, so it works independently of Resolve's on-screen-control mode.

The graph is shown over approximately `-8 EV` to `+6 EV` relative to 18% grey and includes:

- an identity diagonal
- the actual scalar contrast/toe curve
- a middle-grey crosshair
- the current Contrast Pivot point

The display responds live to Contrast, Contrast Pivot, Curve Softness, Toe Amount, and Toe Range. Colour Preserve is not represented because its effective RGB/luminance blend is colour-dependent rather than a single scalar tone curve. The pivot marker follows the true scalar output, so it visibly moves if the pivot itself falls inside the active toe region.

Because this diagnostic is part of the rendered image while enabled, **Show Curve must be switched off before a final render or export**.

### Regularization Scale

ART uses a small ~5 px guided-filter conditioning pass and, for Regularization levels 2-4, a much larger 350 px full-resolution regularization pass.

BaseGrade's **Regularization Scale** changes only the large pass:

- `0.5x` = approximately 175 px
- `1.0x` = ART's original 350 px behaviour
- `2.0x` = approximately 700 px

The small ART conditioning pass remains unchanged. At exactly `1.0x`, BaseGrade calls the original ART-compatible `filterMask()` path directly. Away from `1.0x`, the scaled large radius is snapped to a multiple of five so ART's fast guided filter retains predictable 5x subsampling instead of occasionally falling back to full-resolution filtering at prime radii.

### Current transfers

- DaVinci Intermediate
- Linear

The default is **DaVinci Wide Gamut / DaVinci Intermediate**, matching a common Resolve working pipeline.

For direct comparison against ART, choose **Linear** plus the corresponding `ART ... (D50 parity)` gamut entry.

### Show Colour Map

The diagnostic colour-map positions follow the Tone Equalizer mask, but the preview colours themselves are currently based on linearised sRGB values and are not gamut-converted into the selected working space. They should therefore be treated as approximate outside Rec.709/sRGB. This affects the diagnostic display only, not the Tone Equalizer correction.

## Planned controls

- additional input transfer functions where useful
- further colour and tone controls after the contrast stage is validated

## Build prerequisites

- CMake
- C++17 compiler (Xcode/Clang on macOS)
- Git
- Academy Software Foundation OpenFX SDK checkout

Clone OpenFX beside this project:

```bash
git clone https://github.com/AcademySoftwareFoundation/openfx.git
```

### macOS Apple Silicon

```bash
cmake -S . -B build \
  -DOFX_DIR=$PWD/openfx \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The bundle is produced at:

```text
build/BaseGrade.ofx.bundle
```

For local testing:

```bash
codesign --force --deep --sign - build/BaseGrade.ofx.bundle
sudo mkdir -p /Library/OFX/Plugins
sudo cp -R build/BaseGrade.ofx.bundle /Library/OFX/Plugins/
sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/BaseGrade.ofx.bundle
```

Then fully restart Resolve.

## Tests

The host-independent test suite checks:

- Exposure stop mappings
- zero Temp/Tint identity across every supported gamut and both WB methods
- both WB methods map neutral RGB to the same colourimetric target white
- Linear RGB Gain and Bradford remain distinct on saturated colours
- white-balance continuity around zero Temp/Tint
- neutral-white luminance preservation across gamuts, methods and representative settings
- Tint direction (`+` magenta, `-` green)
- extended Temperature and Tint endpoints
- global contrast identity and pivot behaviour
- requested slope at the pre-toe contrast pivot across Curve Softness values
- deterministic monotonicity sweeps across Contrast, Pivot, Softness and Toe ranges
- Curve Softness roll-off
- positive and negative Toe Amount behaviour and threshold continuity
- prepared per-render contrast constants remain bit-identical to the convenience path
- RGB vs luminance-only Colour Preserve behaviour on ordinary colours
- real Rec.2020 blue is not crushed to black at high Colour Preserve
- low-Y wide-gamut neighbouring pixels remain continuous
- near-black negative-contrast behaviour fades back to the RGB path
- saturated-blue / non-positive-Y safety in the luminance path
- raster Show Curve compositing inside its panel and no modification outside it
- ART-style box-filter border behaviour
- guided-filter subsampling behaviour
- Tone Equalizer smoke tests across Regularization 0-4
- every luminance coefficient row sums to approximately 1
- DaVinci Intermediate's published 18% grey mapping
- DaVinci Intermediate encode/decode round-trips, including negative and HDR values
- BaseGrade's duplicated regularization body remains bit-identical to ART when the effective radius is unchanged
- scaled large radii retain 5x fast-guided-filter subsampling across a radius sweep

Run with:

```bash
ctest --test-dir build --output-on-failure
```

## Licensing and attribution

The Tone Equalizer core is derived from ART's `rtengine/iptoneequalizer.cc` and guided-filter implementation, which in turn contains work derived from darktable and RawTherapee. The derived source is GPL-3.0-or-later; attribution is retained in `teq_core.h`.
