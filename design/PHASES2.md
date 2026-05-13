# TIMBRE Phases 11-15 — Remaining Cleanup

## Phase 11 — TS hygiene + a11y + telemetry tracking (S)
- Fix 8 pre-existing TS errors (App.tsx applyParamToPatch + TelemetryDashboard)
- Bump `--text-tertiary` for WCAG AA contrast on `--bg-raised`
- Wire ToolsDrawer telemetry unseen-count to real wire frame deltas (not assumed)

## Phase 12 — Visualizer real audio (M)
- C++: add `getOutputSamples` native function in WebUiComponent exposing post-amp 1024-sample ring buffer; lock-free SPSC from audio thread
- React: Visualizer consumes via withNativeFunction, replaces simulated synth path
- Sample window properly handles 60fps polling vs audio block boundary

## Phase 13 — Macro routing + audition + retry wiring (M)
- Macros register as ModSource in mod matrix; existing drag-to-assign works for them
- Preset audition-on-hover (300ms delay; ephemeral load; revert on mouse-leave)
- LLM network retry: real bridge call, real countdown, real recovery

## Phase 14 — Preset library + settings expansion (S)
- Grow starter presets from 12 → 30+
- Add Settings: theme override (auto/dark/light), motion override (full/reduced/off), MIDI input selector

## Phase 15 — pluginval re-run + final regression (S)
- Rebuild VST3 with TIMBRE branding
- pluginval --strictness-level 10 against TIMBRE.vst3
- Full ctest sweep
- Git push verification (still 13+ commits ahead of origin)
