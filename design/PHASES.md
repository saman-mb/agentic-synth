# TIMBRE Rebrand — Implementation Phases

UI stack: React + TypeScript rendered in JUCE WebView. Bundle = `juce_add_binary_data`. Source in `ui/src/`.

## Phase 1 — Design Tokens + Foundation (S, 1-2 days)
Replace existing CSS variables with TIMBRE token system. Color (3 bg tiers, accents, mod palette), typography (Inter + JetBrains Mono + Inter Display fallback for Söhne), spacing (4px base), depth/motion/radius scales. Apply globally without restructuring layouts. **Build still works. UI roughly resembles old but with new chrome.**
- Engineers: Frontend Developer, Senior Developer (parallel on disjoint files)
- QA: Code Reviewer, Evidence Collector (screenshot proof)

## Phase 2 — Brand Identity Pass (S, 1 day)
Wordmark TIMBRE, replace "Agentic Synth" everywhere (window title, plugin metadata, README, package.json, JUCE PRODUCT_NAME, splash). Boot animation (1.8s waveform morph + char-by-char wordmark). New favicon/app icon.
- Engineers: Frontend Developer, Brand Guardian (advisory)
- QA: Reality Checker

## Phase 3 — Knob System Rebuild (M, 2-3 days)
New Knob component: layered SVG/Canvas paint, 3 sizes (72/52/36), bipolar variant, mod-active rings (multi-hue source palette), hover/active/drag states, value tooltip with momentum, double-click reset, shift fine, alt bipolar. Vertical drag (not rotational).
- Engineers: Frontend Developer, Senior Developer, UX Architect
- QA: Accessibility Auditor (slider semantics), Code Reviewer, Evidence Collector

## Phase 4 — Layout Restructure (M, 2-3 days)
Replace 2-column accordion with new grid: TopBar (logo, preset, A/B, undo/redo, CPU/MIDI/meter), left sidebar (preset browser with tags), center modules (OSC × 3, Filter, Envelopes, LFOs, FX), right column (visualizer + AI dock + mod matrix), bottom MIDI keyboard. 1280×800 default, aspect-anchored resize.
- Engineers: Senior Developer (lead), Frontend Developer (components), UX Architect (CSS grid)
- QA: Code Reviewer, Evidence Collector, Accessibility Auditor

## Phase 5 — Visualizer (M, 2 days)
Canvas-based oscilloscope + spectrum + XY + wavetable views. Wire to audio buffer via existing WebView bridge (add `getOutputSamples` native function returning post-amp buffer). 60fps render, GPU-cheap, glow trail.
- Engineers: Frontend Developer (Canvas), Senior Developer (JUCE bridge addition)
- QA: Performance Benchmarker, Code Reviewer

## Phase 6 — Preset Browser + A/B + Macros + MIDI Keyboard (L, 3-4 days)
Preset browser sidebar (tag filter pills, audition on hover, favorites, search, init patch). A/B compare on TopBar (snapshot + copy A→B, spacebar toggle). 4 macro knobs (assignable, renamable). MIDI keyboard component bottom (octave switch, mod wheel, pitch bend).
- Engineers: Frontend Developer × 2, Senior Developer (state mgmt)
- QA: UX Researcher (workflow validation), Code Reviewer, Evidence Collector

## Phase 7 — AI Prompt Theatre (M, 2-3 days)
Always-visible prompt dock with violet→magenta gradient sweep idle animation. On submit: cyan underline sweep, reasoning ticker, scope flatline+dim, knob settle animation staggered in signal-flow order, 800ms auto-preview. Cmd+K focus shortcut. Voice transcription waveform → OSC1 wavetable morph on release.
- Engineers: Frontend Developer, Senior Developer (audio scheduling)
- QA: Reality Checker, Evidence Collector

## Phase 8 — Modulation Matrix (L, 3-4 days)
Mod ring rendering on knobs (animated stroke-dasharray in source color, oscillates with mod waveform). Drag-to-assign: drag colored dot from source onto destination knob. Mod amount via knob halo. Constellation view (3D nodes/threads) as alt mod-matrix tab. Both backed by same data.
- Engineers: Senior Developer (canvas constellation), Frontend Developer (mod rings), UX Architect (interaction model)
- QA: UX Researcher, Accessibility Auditor (drag accessibility), Code Reviewer

## Phase 9 — Motion + Polish + Failure States (M, 2 days)
Custom easing curve, staggered choreography, signature cyan underline-sweep flourish on confirmations. Boot animation, patch-load knob settle, paper-fold section collapse, value tooltip momentum, muted badge breathing, telemetry radar sweep. Failure states (mic unavailable, network error, empty prompt) without dev-leak ugliness.
- Engineers: Whimsy Injector (motion choreography), Frontend Developer
- QA: Reality Checker, Evidence Collector

## Phase 10 — Easter Eggs + UI Audio + Final QA (S, 1 day)
`sudo make me a sound` prompt easter egg. Option+double-click logo for 360° knob spin. Voice-transcribe confirmation pip. Patch-load tape-stop thunk (opt-in). Final accessibility audit, cross-platform sanity, regression sweep.
- Engineers: Frontend Developer
- QA: Accessibility Auditor (full WCAG pass), Reality Checker, Code Reviewer

---

## Workflow Per Phase

1. Spawn 1-3 engineers in parallel (file-disjoint where possible)
2. Each builds in isolated build dirs (`build-a`, `build-b`...) or sequential if conflicts
3. Clean up build dirs at end
4. Run merged build + tests
5. QA pass in parallel (Code Reviewer + relevant specialist)
6. Address feedback in follow-up engineer pass
7. Commit phase
8. Move to next phase
