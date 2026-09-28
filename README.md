# BaseGrade

BaseGrade is an OpenFX image-grading plugin aimed at bringing photo-oriented primary controls to DaVinci Resolve and other OFX hosts.

The project began with a close port of ART's Tone Equalizer. The `main` branch is the known-working ART-parity baseline; development branches add BaseGrade-specific features while keeping the ART-derived core clean and independently testable.

## Current feature branch

`feature/basegrade-input-and-regularization`

This branch adds:

- plugin/bundle name changed to **BaseGrade**
- stable plugin identifier `io.github.ryancara.BaseGrade`
- **Input Gamut** selection
- **Input Transfer** selection
- DaVinci Wide Gamut support
- native DaVinci Intermediate decode/process/re-encode
- **Detail** renamed in the UI to **Regularization**
- **Regularization Scale**, with `1.0x` equal to ART's original spatial scale
- ART parity gamut options retained for comparison testing

No hidden gamut conversion is performed. BaseGrade decodes the selected transfer to scene-linear RGB, keeps the RGB values in the selected gamut, uses that gamut's RGB-to-XYZ Y coefficients to build the Tone Equalizer mask, applies the correction in linear light, and then re-encodes to the selected transfer.

### Current transfers

- DaVinci Intermediate
- Linear

The default is **DaVinci Wide Gamut / DaVinci Intermediate**, matching a common Resolve working pipeline.

For direct comparison against ART, choose **Linear** plus the corresponding `ART ... (D50 parity)` gamut entry.

## Planned controls

- Exposure
- Temperature
- Tint
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

- ART-style box-filter border behaviour
- guided-filter subsampling behaviour
- Tone Equalizer smoke tests across Regularization 0-4
- DaVinci Intermediate's published 18% grey mapping
- DaVinci Intermediate encode/decode round-trips, including negative and HDR values

Run with:

```bash
ctest --test-dir build --output-on-failure
```

## Licensing and attribution

The Tone Equalizer core is derived from ART's `rtengine/iptoneequalizer.cc` and guided-filter implementation, which in turn contains work derived from darktable and RawTherapee. The derived source is GPL-3.0-or-later; attribution is retained in `teq_core.h`.
