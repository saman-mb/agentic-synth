# JSI harness (mock)

Minimal Node stand-in for how React Native would attach the native module.
Not an Nx app and not `apps/mobile`.

On device, the native addon installs a JSI host object wrapping `ags_engine_*`.
JS then constructs `JsiSynthEngine` from that binding. Audio is a native
AudioStream calling `ags_engine_render` on the RT thread — JSI is control-rate
only.

## What RN would call

```js
import { NativeModules } from 'react-native';
import { JsiSynthEngine } from '@agentic-synth/engine-bridge';

// Once at startup: install the JSI host (returns true, attaches global).
NativeModules.Agsynth.install();

const engine = new JsiSynthEngine(global.__AgsynthNative);
await engine.ensureStarted(); // no-op, or binding.start() if the stream needs it
engine.setPatch(patch);       // packPatchParams on the JS thread
engine.setParam('filter.cutoff_hz', 1200);
engine.noteOn(60, 100);
```

`createSynthEngine()` is unchanged and still returns `WasmSynthEngine` for web.

## Run the mock

```sh
node tests/jsi-harness/install-mock.mjs
```

The mock's `install()` attaches `globalThis.__AgsynthNative` with the same
method names as the real binding (`setPatch`, `setParam`, `noteOn`, `noteOff`,
`dispose`, `recreate`). Return codes are `0` (AGS_OK); a non-zero code is what
`JsiSynthEngine` maps to `AgsynthError`.
