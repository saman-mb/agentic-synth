# engine-bridge

WASM and WebAudio implementations of the synth engine, behind one TypeScript interface. The factory returns the WASM engine; WebAudio stays in tree for #307 golden extraction. Native JSI is a sibling `JsiSynthEngine` constructed from the RN harness — the web factory is unchanged.

## Public surface

- `SynthEngine` — `setPatch` (loadPatch), `noteOn` / `noteOff` / `playMidiNote` (trigger), `setParam` / `applyMacros` (render-params), plus `ensureStarted`, `getScopeSamples`, `setOutputDevice`, `dispose`.
- `createSynthEngine()` — returns the current implementation (`WasmSynthEngine`). Missing WASM or module-init failure rejects `ensureStarted()`; there is no silent WebAudio fallback.
- `WebSynthEngine` — still constructible for #307 golden extraction. Not the factory default.
- `JsiSynthEngine` / `AgsynthError` — native JSI sibling; construct from the RN harness. Not the factory default.
- `setPatchParam` — dotted-path mutation used by the demo shim's patch snapshot. Not a second engine API.

`VoiceManager` and `EffectRack` are not exported.

## Implementations

| Backend | Status | Location |
| --- | --- | --- |
| C++ / WASM | current factory default | `wasmEngine`; artefacts `dist/wasm/agsynth.js` + `agsynth.wasm` from `npx nx run wasm:build-wasm` |
| WebAudio | in tree for #307 goldens | `src/lib/` (`engine.ts`, `voices.ts`, `effects.ts`, `paramMap.ts`) |
| JSI | sibling; construct `JsiSynthEngine` from the RN harness | `jsiEngine`; native AudioStream, not the web factory |

One factory, no impl-picker options.

## Dependencies

Allowed: `@agentic-synth/shared-types`, `@agentic-synth/data`.

Not allowed: React, `apps/`, Netlify functions, or any UI module. `makeFallbackPatch` restates the KnobGrid default in this lib so the engine does not import React.

## Callers

The web-demo shim (`apps/web/src/demo/juceShim.ts` and a type-only import from `generateFlow.ts`) is the only consumer. The plugin WebView uses native C++ through `window.__JUCE__` and does not load this lib (lazy chunk, #285).
