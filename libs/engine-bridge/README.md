# engine-bridge

WebAudio (and later WASM/JSI) implementations of the synth engine, behind one TypeScript interface. This is the seam where a C++/WASM engine (#292) will slot in; this library does not implement WASM.

## Public surface

- `SynthEngine` — `setPatch` (loadPatch), `noteOn` / `noteOff` / `playMidiNote` (trigger), `setParam` / `applyMacros` (render-params), plus `ensureStarted`, `getScopeSamples`, `setOutputDevice`, `dispose`.
- `createSynthEngine()` — returns the current implementation (`WebSynthEngine`).
- `setPatchParam` — dotted-path mutation used by the demo shim's patch snapshot. Not a second engine API.

`VoiceManager` and `EffectRack` are not exported.

## Implementations

| Backend | Status | Location |
| --- | --- | --- |
| WebAudio | current | `src/lib/` (`engine.ts`, `voices.ts`, `effects.ts`, `paramMap.ts`) |
| C++ / WASM / JSI | later (#292) | same `SynthEngine` type; factory swap, not a new API |

Today there is one factory and no impl-picker options.

## Dependencies

Allowed: `@agentic-synth/shared-types`, `@agentic-synth/data`.

Not allowed: React, `apps/`, Netlify functions, or any UI module. `makeFallbackPatch` restates the KnobGrid default in this lib so the engine does not import React.

## Callers

The web-demo shim (`apps/web/src/demo/juceShim.ts` and a type-only import from `generateFlow.ts`) is the only consumer. The plugin WebView uses native C++ through `window.__JUCE__` and does not load this lib (lazy chunk, #285).
