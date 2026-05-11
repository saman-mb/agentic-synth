# Build & Release

## Prerequisites

- CMake 3.28+
- JUCE 7 (submodule): `git submodule update --init --recursive`
- C++20 compatible compiler (GCC 13+, Clang 16+, MSVC 2022+)

## Build from Source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Targets:
- `AgenticSynth` — Standalone application
- `AgenticSynth_VST3` — VST3 plugin
- `AgenticSynth_AU` — Audio Unit (macOS only)
- `agentic_synth_tests` — Unit tests

## Platform-Specific Builds

### macOS
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

### Windows
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -A x64
cmake --build build
```

### Linux
```bash
sudo apt install libasound2-dev libjack-dev libcurl4-openssl-dev \
  libfontconfig-dev libgl-dev libgtk-3-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Code Signing & Notarization

### macOS
```bash
# Codesign
codesign --force --sign "Developer ID Application" --deep --timestamp \
  build/AgenticSynth.app
codesign --force --sign "Developer ID Application" --deep --timestamp \
  build/AgenticSynth.vst3

# Notarize
xcrun notarytool submit build/AgenticSynth.dmg \
  --apple-id user@example.com \
  --team-id TEAMID \
  --password @keychain:AC_PASSWORD \
  --wait
```

### Windows (Authenticode)
```powershell
signtool sign /fd SHA256 /a /f certificate.pfx /p password \
  build/AgenticSynth.exe
```

### Linux (AppImage)
```bash
./scripts/linux-appimage.sh
```

## Release Artifacts

- macOS: .dmg (signed + notarized)
- Windows: .exe installer (Authenticode signed)
- Linux: .AppImage

See `.github/workflows/ci.yml` for the automated release pipeline.
