# BaseGrade

BaseGrade is an OpenFX image-grading plugin aimed at bringing photo-oriented primary controls to DaVinci Resolve and other OFX hosts.

The current codebase begins with a working port of ART's Tone Equalizer. The first development goal is to preserve a known-good ART-parity baseline before adding BaseGrade-specific features.

## Current status

The current OFX build has been compiled on Apple Silicon macOS against the Academy Software Foundation OpenFX SDK and loaded successfully in DaVinci Resolve.

The Tone Equalizer currently provides:

- Blacks
- Shadows
- Midtones
- Highlights
- Whites
- Pivot
- ART-style spatial regularisation
- ART working-space luminance matrices
- diagnostic colour-map preview

At this baseline stage the plugin expects **scene-linear float RGB(A)** input. In Resolve, use a CST to linear before the plugin and a CST back afterwards.

## Roadmap

Planned BaseGrade controls include:

- native input gamut selection
- native input transfer/gamma selection, including DaVinci Intermediate
- Exposure
- Temperature
- Tint
- Tone Equalizer
- Regularization
- Regularization Scale
- Contrast
- adjustable luminance-only contribution for Contrast

The ART-compatible behaviour will remain the reference/default for the Tone Equalizer while BaseGrade-specific extensions are added separately.

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
build/ArtToneEq.ofx.bundle
```

For local testing, ad-hoc signing may be useful:

```bash
codesign --force --deep --sign - build/ArtToneEq.ofx.bundle
```

Install system-wide on macOS:

```bash
sudo mkdir -p /Library/OFX/Plugins
sudo cp -R build/ArtToneEq.ofx.bundle /Library/OFX/Plugins/
sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/ArtToneEq.ofx.bundle
```

Then fully restart Resolve.

## ART parity

The Tone Equalizer core is derived from ART's `rtengine/iptoneequalizer.cc` and guided-filter implementation, which in turn contains work derived from darktable and RawTherapee.

For parity testing, use matching linear working spaces in ART and the plugin, render at full resolution, and compare results in EV. Spatially regularised cases should be compared with a small numerical tolerance because ART uses single-precision recursive filtering.

## Licence

GPL-3.0-or-later. See source-file attribution and `LICENSE`.
