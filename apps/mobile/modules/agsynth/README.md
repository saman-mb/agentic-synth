# Agsynth Expo module (stub)

Placeholder native module for Tambra mobile JSI bridge (#316).

## Wiring (future)

1. **iOS** — `AgsynthModule.swift` calls into `src/jsi/` host; `install()` attaches `__AgsynthHost` on the JS runtime.
2. **Android** — `AgsynthModule.kt` loads `.so` from CMake `agsynth_mobile` target; same `install()` contract.
3. **JS** — `apps/mobile/src/engine/createMobileEngine.ts` prefers `NativeModules.Agsynth.install()`, falls back to mock in dev/CI.

## Dev client

Build with EAS development profile (`eas.json`) so Skia + custom native code run on device. Expo Go cannot load this module until published prebuild.
