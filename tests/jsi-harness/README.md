# JSI harness (mock)

Minimal Node stand-in for how React Native would attach the native module.
Not an Nx app and not `apps/mobile`. The Expo app that would consume this
host ([#294](https://github.com/saman-mb/agentic-synth/issues/294)) is
residual and is not in this PR. On-device Pixel-class RT measurement is
also residual.

On device, `NativeModules.Agsynth.install()` returns true and attaches
`global.__AgsynthHost` — a JSI HostObject wrapping `ags_engine_*`. It does
not return the binding. `NativeModules.Agsynth` is the install TurboModule;
it is not the live HostObject (there is no `NativeModules.Agsynth.noteOn`).

Audio is a native AudioStream (not Expo AV) calling `ags_engine_render` on
the RT thread — JSI is control-rate only.

## What RN would call

```js
import { NativeModules } from 'react-native';

// Once at startup: install the JSI host (returns true, attaches global).
const ok = NativeModules.Agsynth.install(); // true — not the binding

const host = global.__AgsynthHost;
host.create(48000);
host.setPatch(patchBytes);
host.setParam('filter.cutoff_hz', 1200);
host.pushEvents(eventBytes); // packed ags_event[]
host.start();
```

HostObject methods: `create`, `destroy`, `setPatch`, `setParam`,
`pushEvents`, `processBlock`, `renderOffline`, `start`, `stop`,
`saveState`, `loadState`, `recreate`.

`JsiSynthEngine` is the TS `SynthEngine` sibling (`noteOn` / `setPatch` /
…). The Expo app that would construct it from this host is residual (#294).
`createSynthEngine()` is unchanged and still returns `WasmSynthEngine` for web.

## Run the mock

```sh
node tests/jsi-harness/install-mock.mjs
```

The mock's `install()` attaches `globalThis.__AgsynthHost` with the same
method names as the C++ HostObject. Return codes are `0` (AGS_OK); a
non-zero code is what `JsiSynthEngine` maps to `AgsynthError`.
