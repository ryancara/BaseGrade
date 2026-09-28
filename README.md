# BaseGrade

BaseGrade is an OpenFX image-grading plugin aimed at bringing photo-oriented primary controls to DaVinci Resolve and other OFX hosts.

The project began with a close port of ART's Tone Equalizer. The `main` branch is the known-working BaseGrade foundation; development branches add new primary controls while keeping the ART-derived core clean and independently testable.

## Current feature branch

`feature/white-balance`

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
- ART parity gamut options retained for comparison testing

No hidden gamut conversion is performed. BaseGrade decodes the selected transfer to scene-linear RGB, keeps the RGB values in the selected gamut, applies Exposure and white balance, builds the Tone Equalizer mask using that gamut's RGB-to-XYZ Y coefficients, applies the Tone Equalizer correction in linear light, and then re-encodes to the selected transfer.

### Exposure

Exposure is a scene-linear photographic gain applied before white balance and the Tone Equalizer:

- `+1 EV` = 2x linear RGB
- `0 EV` = identity
- `-1 EV` = 0.5x linear RGB

Because Exposure is upstream of the Tone Equalizer, changing exposure naturally moves image content through the equalizer's tonal zones.

### Temperature and Tint

Temperature and Tint are centred at zero and use the same target-white calculation for both white-balance methods.

**Temperature** is a relative reciprocal-colour-temperature shift. One slider unit equals one mired. Positive values warm the image and negative values cool it. The zero point is anchored to the selected gamut's actual reference white, so `0` remains identity for D65, D60 and D50 working spaces.

**Tint** moves perpendicular to the Planckian locus in CIE 1960 `u,v`. Positive values move toward magenta and negative values toward green. The current mapping is 0.0002 `u,v` units per slider unit.

The two white-balance methods apply that same target white differently:

- **Linear RGB Gain** converts the target white to the selected working RGB space and applies per-channel scene-linear gains.
- **Bradford** converts through XYZ and Bradford's cone-response basis, applies the source-to-target white scaling there, then returns to the selected working RGB space.

This makes the dropdown a fair A/B test: changing the method does not change the Temperature/Tint curve itself.

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

- Contrast
- Contrast luminance/RGB mix
- additional input transfer functions where useful

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
