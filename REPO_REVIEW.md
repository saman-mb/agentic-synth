# Agentic Synth — Multi-Specialist Repo Review

**Date:** 2026-05-11
**Repo:** `/Users/saman/Dev/agentic-synth` @ `master` (`be24808`)
**Reviewers (sub-agents):** Software Architect · Senior Developer · Reality Checker (QA) · UX Architect · Product Manager · DevOps Automator · SRE · DSP/Audio Engineer

---

## Executive Summary

Agentic Synth is an ambitious agent-driven JUCE synth plugin with a React companion UI. The **vision is crisp**, the **building blocks show senior-level thinking** (SPSC queue, RT discipline, POD patch contract, ZDF filters, GBNF-constrained generation), but the **system is not assembled**:

- README claims "early scaffold"; `src/` already contains ~70 files implementing streaming LLM, telemetry, session memory, knob bridge — the docs and the code disagree about what exists.
- `third_party/JUCE` submodule is empty; `cmake -S . -B build` **hard-fails on a fresh clone**.
- CI is **bricked** — runs have been queued 18+ hours on a self-hosted runner that isn't responding; last 4 runs are cancelled/failed; last *passing* run executed a single placeholder assertion.
- The JUCE plugin GUI is a single "Agentic Synth — ready." label. The React UI is the only real surface.
- Modulation, velocity, FX, mod-matrix, stereo, and ~half the `PatchStruct` fields are not wired through to audio output.
- Multiple "Acceptance" tests are misnamed smoke tests; three test files are orphaned from the build target.

**Verdict:** Scaffold with theatre. Strong individual components, weak integration, false-confidence test naming, and a vision/state gap that misleads contributors. **Not ship-ready.** A focused 4–6 week effort scoped to macOS + AU + single-box inference can produce a real v0.1 demo.

### Top 10 cross-cutting risks (P0–P1)

1. **Build broken out-of-the-box** — empty JUCE submodule + hard `add_subdirectory` → fresh clone fails. (Arch, DevOps, QA)
2. **CI not running** — self-hosted runner stalled, no `ubuntu-latest` fallback, plugin never compiled in CI on any platform. (DevOps, QA)
3. **Acceptance test theatre** — `Phase3AcceptanceTest` "Plugin is stable in DAW" never instantiates a plugin; `PatchStateManagerTest:54-59` asserts nothing; 3 test files silently orphaned from build. (QA)
4. **Audio engine not assembled** — `SynthEngine.cpp` is a placeholder, LFOs never consumed by `Voice::render`, velocity ignored, filter envelope unwired, no FX implementation. (DSP)
5. **PatchStruct delivered without per-sample smoothing** → every knob move and every agent patch is a zipper-noise event. (DSP, Eng)
6. **WebSocketBridge in plugin process is fragile** — port collision on multi-instance DAW, no fallback, no discovery, detached threads on shutdown. (Arch, SRE)
7. **Mutex on the SPSC pipeline producer side** breaks the type's stated contract; mixing locks into RT-adjacent paths. (Arch, Eng)
8. **Hand-rolled JSON in `StreamParser` and `SemanticMapper`** — same bug class already fixed once in `AgentBridge`. (Eng)
9. **`Knob.tsx` has no `role="slider"` or keyboard support** — entire UI unusable with keyboard/screen reader. (UX)
10. **Scope > 5 years of solo work** — VST3+AU+Standalone × mac+win+linux × agent + DSP + UI + local LLM + two-box LAN. Wedge undefined. (Product)

---

## Architecture Review

### Strengths

- **Clean layering with right dependency direction.** `src/CMakeLists.txt:1-61` enforces `mapper → engine → agent → plugin`. Mapper has zero JUCE dep (`mapper/SemanticMapper.h`, `GrammarSampler.h`), keeping NL parsing pure and unit-testable. Engine never gets agent symbols linked in.
- **Realtime discipline is real.** `src/engine/SPSCQueue.h` is a textbook wait-free SPSC with `alignas(64)` cache-line padding and `drain_latest` for stale-patch coalescing. `RealtimeSafety.h` provides `ScopedRealtimeContext` + opt-in `REALTIME_STRICT` `operator new` override.
- **POD patch contract.** `PatchStruct.h:54-76` is the right shared kernel — fixed-size POD with `static_assert` size guards crossing thread/process boundaries. Load-bearing decision, correctly made.
- **ADR discipline started.** `docs/adr/ADR-0001-initial-architecture.md` follows MADR; `docs/adr/template.md` exists.

### Concerns (ranked)

1. **README says "early scaffold" but `src/` is a half-system.** `AgentBridge.h:23-122` implements ~10 issues. Either the README is stale or the implementation outran the design. Reconcile.
2. **Submodules uninitialised but CMake adds them unconditionally.** `third_party/JUCE/` is empty; `.gitmodules` only registers JUCE (no llama.cpp). Root `CMakeLists.txt:17` `add_subdirectory(third_party/JUCE)` fires for plugin OR tests → fresh clones fail.
3. **llama.cpp integration is undocumented vs implemented.** `third_party/llama.cpp/README.md` references `LlamaClient.h/.cpp` that don't exist; actual LLM calls happen in `mapper/GrammarSampler.cpp` and `mapper/SemanticMapper.cpp` via HTTP. No `add_subdirectory(third_party/llama.cpp)` anywhere. Pick a story (in-process llama vs external server) and document.
4. **`PrePatchPipeline` producer mutex breaks the SPSC contract on paper.** `PrePatchPipeline.h:61` admits `producerMutex_` serialises producers because submit/refinePatch/injectPatch are called from different threads. Rename to `MPSCPatchPipeline` or use a real MPSC structure.
5. **`AgentBridge` is a god object.** Owns pipeline, variation engine, session memory, sampler, semantic mapper, stream parser, telemetry, WS pointer, MIDI atomics. Split into `PromptHandler`, `KnobBridge`, `DictionaryService`, `TelemetryService` behind a `WsRouter`.
6. **WebSocketBridge inside the plugin process is risky.** `src/agent/WebSocketBridge.h:29` runs JUCE thread + per-client `std::thread`s inside a DAW-hosted VST3. Sandbox prompts, firewall prompts, port collision on multi-instance load. Move into standalone-only or make opt-in.
7. **`CMakePresets.json` has no `linux-*` presets** despite CI/local-inference targeting Linux. `windows-debug` lacks `CMAKE_CXX_STANDARD`.
8. **`AGENTIC_SYNTH_BUILD_TESTS=ON` triggers JUCE download** because `PatchStateManager` uses `ValueTree`. Split engine into `engine_core` (JUCE-free) + `engine_juce`.

### Recommendations

- Write ADRs for the **actual** load-bearing decisions: SPSC patch queue contract, in-process WebSocket bridge, POD `PatchStruct` as IPC kernel, in-process vs external llama.cpp.
- Split engine into JUCE-free core; tests link only `engine_core`.
- Adopt an anti-corruption layer (`WsRouter`) between WS text protocol and domain logic.
- Fix submodule UX with `FetchContent_Declare` + pinned `GIT_TAG`, fall back to populated `third_party/` if present.
- Reconcile README + commit history. Add `linux-debug`/`linux-release` presets.

---

## Engineering Review

### Strengths

- **SPSC queue is correct.** `src/engine/SPSCQueue.h:46-72` — textbook acquire/release ordering, power-of-two mask, `alignas(64)` against false sharing, `is_trivially_copyable_v` static_assert. `drain_latest()` is a nice RT primitive.
- **RT discipline is real, not theatrical.** `RealtimeSafety.h:53-65` provides opt-in `operator new`/`delete` overrides (commit `085751c` even fixed an ODR violation). `MorphEngine::setPosition` (`MorphEngine.cpp:56-61`) explicitly defers `std::function` callbacks to a polled UI thread. Senior-level call.
- **DSP is solid where it exists.** `Filter.cpp:42-69` MoogLadder closed-form ZDF, `Filter.cpp:111+` Cytomic SVF. `VoiceManager.cpp:42` reserves before resize.
- **Tooling actually catches things.** `.pre-commit-config.yaml` + CI clang-format `--Werror`; commits `aafd745`, `1b3214a`, `e8ad842` show real format catches.

### Critical Issues

- **Audio thread reachable via mutexed pipeline.** `AgenticSynthPlugin::processBlock` calls `agentBridge_.pollPatch()`; producer side holds `std::lock_guard<std::mutex>` in `PrePatchPipeline.cpp:88-93`. `currentPatch()` (l.95) also locks. Document thread affinity, audit consumer path, and either remove the lock or rename the abstraction.
- **Hand-rolled JSON in `StreamParser` + `SemanticMapper`** uses `std::sscanf` on substrings (`StreamParser.cpp:23, 39, 205, 213, 217`) and `find("\"key\"")` substring matching (`SemanticMapper.cpp:369-408`). Commit `91d5e4b` already fixed this same bug class in `AgentBridge` by switching to `juce::JSON`. Mapper/StreamParser still vulnerable.
- **Silent parse failures.** `StreamParser::applyField` (l.205-217) ignores `sscanf` return and emits no telemetry → malformed LLM output is invisible.
- **`extractFloat`/`extractInt` ignore `sscanf` return value** (`StreamParser.cpp:22-24`) → malformed inputs silently retain fallback.

### Code Smells

- **Weak local pre-build discipline.** Commit sequence `abf32aa → f67341d → e1ed455 → 5d2c373 → d104655 → be95306` is missing-include / link / `NOMINMAX` / `strtof` churn — changes pushed without cross-platform local builds. Self-hosted runner made remote builds cheap and encouraged "push and see."
- **Designated-initializer order bug `1b787a7`** shipped to CI. Either `-Wreorder-init-list` is off, or `-Werror` not on that TU.
- **`App.tsx:14`** uses `JSON.parse(JSON.stringify(patch))` to clone, then `as Record<string, number>` casts 7 times (l.18-31) to mutate. `PatchParams` type buys nothing. Use `structuredClone` + a typed param-path setter.
- **`SemanticMapper::http_post_embedding`** (`SemanticMapper.cpp:133-220`) hand-rolls BSD sockets + HTTP/1.0 once per keyword inside `best_match` → O(tokens × dataset_size) round-trips. Comment at l.286 says "cached in a real implementation." Use libcurl or `juce::URL` + cache embeddings at startup.
- **Empty/stub modules:** `SynthEngine.cpp:5-9` (placeholder, returns silence), `WhisperClient.cpp:9,18` (TODO), `PresetDatabase.cpp:16,24` (TODO), `GenerationService.cpp:109` (TODO). Acceptance tests pass against placeholders = false confidence.
- **Duplicate lerp implementations:** `MorphEngine::lerp` (`MorphEngine.cpp:76`) and `lerpPatch` (`PrePatchPipeline.cpp:9`) have already drifted on discrete-field handling.
- **`paramToDelta`** (`AgentBridge.cpp:24-174`) is a 150-line if-else with `try/catch(...)` around `std::stoi`. Adding a parameter requires touching this file, `StreamParser`, validator, `KnobGrid`, and `applyParamToPatch` in `App.tsx` — five places, no compile-time link.

### Recommendations

1. Replace `PrePatchPipeline::producerMutex_` with a true SP serialization point or a second SPSC queue. Document thread affinity in every public method.
2. Annotate audio thread: `processBlock` should construct `ScopedRealtimeContext`. Build CI with `REALTIME_STRICT`.
3. Replace hand-rolled JSON in `StreamParser` and `SemanticMapper` with `juce::JSON`.
4. Add `-Werror -Wall -Wextra -Wpedantic -Wreorder-init-list` on engine_lib and agent_lib.
5. Pre-push hook running `cmake --build` on at least one platform.
6. Generate `paramToDelta`/`applyField`/`applyParamToPatch` from one schema (the GBNF + `PatchStruct`).
7. Cache `SemanticMapper` keyword embeddings at startup.
8. Either finish or delete `SynthEngine`/`WhisperClient`/`PresetDatabase` stubs.
9. Tighten `App.tsx` types: `structuredClone` + typed path setter.

---

## Test & Quality Reality Check

### What Actually Works
- **Test file content is real, not stubs.** `Phase1AcceptanceTest.cpp:36-49` exercises `HeuristicParser` against 25 descriptors with cutoff bounds; `Phase1AcceptanceTest.cpp:51-75` renders 1024 samples and checks peak/clipping; `RealtimeSafetyTest.cpp:53-79` runs a 100k-iteration SPSC stress test with real producer/consumer threads.
- **Pre-commit hooks are enforced and legitimate** (`.pre-commit-config.yaml:5-46`).
- **27 of 28 test files compile against real code** (Catch2 v3 via FetchContent, `tests/CMakeLists.txt:1-8`).

### What's Theatre
- **Build is broken right now.** `cmake -S . -B /tmp/build-verify` fails at `CMakeLists.txt:17` — `third_party/JUCE/` is empty (verified `ls`).
- **Last green local test run executed exactly ONE assertion.** `build/Testing/Temporary/LastTest.log` (May 8): *"placeholder test target is wired into CI ... All tests passed (1 assertion in 1 test case)"*. The 28-file suite has never been observed running in this checkout.
- **"Phase 3 Acceptance" is mislabeled.** `Phase3AcceptanceTest.cpp:12-30` claims "Plugin is stable in DAW-like conditions" but never instantiates the plugin or a DAW host — just calls `VoiceManager::renderNextSample()` in a loop and asserts `std::isfinite()`.
- **`Phase2AcceptanceTest.cpp:36-47`** "NL refinement" parses two independent strings and asserts both have `cutoff_hz > 0`. Comment says "refine mutates internal state" — it doesn't.
- **`WhisperClientTest.cpp`** only tests negative paths (uninitialized, missing files, empty input). Zero coverage of actual transcription.
- **`PatchStateManagerTest.cpp:54-59`** loads invalid XML, then does `(void)result;` — asserts nothing.

### Coverage Gaps
**Orphaned from build** (exist on disk but excluded from `tests/CMakeLists.txt:11-35`):
- `MorphEngineTest.cpp` (172 lines)
- `PatchStateManagerTest.cpp` (59 lines)
- `SPSCQueueTest.cpp` (135 lines)

**No matching test:** `SynthEngine`, `DCBlocker`, `AgentBridge` (only indirect via Phase2/3), `GenerationService`, `PrePatchPipeline` (only via `PrePatchTest`), `Telemetry.cpp`, `WebSocketBridge`, entire `plugin/` layer, `MainComponent`, `Main.cpp`, `mapper/UnsafeMode.h`.

### Build / CI Status
- CI workflow structurally sound (lint job + tests job, gated).
- **CI is not green and not running.** `gh run list --limit 5`: most recent run queued **18h38m** on `[self-hosted, Linux, X64, unit-test]`. Prior 3 runs: cancelled, cancelled, failure (JUCE link error). Last completed run was the link-failure on May 8.

### Verdict
**Scaffold with theatre, not ship-ready.** Test code is mostly real (good faith effort), but: build broken out-of-the-box, 3 test files silently orphaned, CI red 4+ runs, last green run was 1 placeholder assertion, "DAW stability" suite never instantiates a plugin. Mislabeled acceptance tests are the most concerning signal — project wants to look further along than it is. README is honest; test naming is not.

### Verification commands
```
cmake -S . -B /tmp/build-verify   # → fail at CMakeLists.txt:17 (missing JUCE)
ls third_party/JUCE/              # → empty
gh run list --workflow=ci.yml --limit 5   # → queued 18h, prior cancelled/failure
cat build/Testing/Temporary/LastTest.log  # → 1 placeholder assertion
```

---

## UX Design Review

### Current State
Two surfaces, very different maturity:

- **Web UI (`ui/`, React 19 + Vite, no design-system deps).** Two-pane shell: left `aside.panel-knobs` with tabs (`Knobs` / `Dictionary` / `Telemetry`), right `main.panel-chat` hosting `ChatInterface`. Conversation is real: prompt or `PushToTalk` STT → bridge streams `token` → `patch` → `rationale` → `done`, agent-edited knobs flash purple (400 ms `knob-flash` keyframe). Per-message `Like / Pass`, optional A/B `ABVariationGrid`, collapsible "Why this patch?" rationale, `Suggested variations` strip. No patch history, save/load, preset browser, or undo.
- **JUCE surface (`src/MainComponent.cpp`).** Centered `welcomeLabel` reading *"Agentic Synth — ready."* in a 600×400 window. No GUI parity.

Styling: hand-rolled CSS, dark-only palette (`#111018` bg, `#7b5cff` accent), no design tokens, no light mode.

### UX Risks
- **JUCE app is empty.** A DAW-loaded plugin shows only a label. The companion-UI promise breaks the moment the web app isn't running. WS bridge (`ws://localhost:9002`, `ws://localhost:8765`) failure surfaces only as a `StatusDot` tooltip.
- **`KnobGrid` is overwhelming.** ~50 knobs (3 osc × 7 + filter + 2 envs + 2 LFO + reverb + delay + global) in a 340–420 px column — opposite of the natural-language premise.
- **Agent "diff" is ephemeral.** `agentKeys` clears after 500 ms. No visual record of which knobs the agent changed vs which the user touched.
- **No audition affordance.** No play button, no QWERTY keyboard, no preview note. Web UI alone cannot make sound.
- **A/B variations have no commit action.** `ABVariationGrid` shows two `PatchPreview` cards but no "use A" / "use B" CTA — dead-end UI.
- **`PushToTalk` uses deprecated `ScriptProcessorNode`** (`PushToTalk.tsx:27`) and silently drops audio errors.
- **Feedback bar disables after one click** — weak training signal, no comments.
- **Mobile breakpoint stacks panels at 900 px** but knob hit targets (64 px, `ns-resize` drag) aren't touch-tuned and no keyboard fallback.

### Producer Workflow Fit
Mental model (describe → tweak → refine) is right; `Knob.tsx` vertical-drag matches DAW convention. Missing: preset/patch browser, A/B/init/randomize row, undo/history, MIDI learn, mod-matrix view, section collapse, tooltips. Sections labeled with abbreviations (`Reso`, `EnvMod`, `FM Rt`) but the NL entry point implies less-expert audiences too. Key/scale context from prompt is not surfaced in UI state.

### Accessibility Notes
- Good: `aria-live="polite"` on message list, `aria-label` on regions, `visually-hidden` label on textarea, `aria-pressed` on feedback buttons, `useId()` for input.
- **`Knob.tsx` has no `role="slider"`, no `aria-valuenow/min/max`, no keyboard handlers.** Pointer-drag only — unusable with keyboard or screen reader. **#1 a11y defect.**
- `panel-tab` buttons lack `role="tab"` / `aria-selected` / `aria-controls` — not a real tablist.
- Color-only signal for agent-edited knobs (purple flash) — no text or `aria-live` echo.
- No focus-visible styles. Dark-only theme, no `prefers-color-scheme`.

### Recommendations
1. **Give JUCE surface real UI parity or kill it.** Either mirror the React knob grid in JUCE or ship the web UI as a WebView inside the plugin window.
2. **Make `Knob.tsx` accessible.** `role="slider"`, `tabIndex={0}`, `aria-valuenow/min/max/valuetext`, arrow/shift-arrow/home-end handlers, `aria-live` echo on agent edits.
3. **Progressive disclosure on `KnobGrid`.** Collapse osc 2/3, filter env, LFO 2, delay by default; pin 6–8 macros.
4. **Persist agent edit history per message.** Sticky badge per agent-edited knob, clearable on next prompt; per-knob revert.
5. **Add audition.** In-browser preview: QWERTY-as-MIDI or "play C3 1s" button next to each generated patch.
6. **Make A/B actionable.** `Use A` / `Use B` / `Blend` inside `ABVariationGrid`.
7. **Surface prompt semantics.** Chip strip under user message ("dark", "wide", "D minor", "pad") that the user can toggle to refine.
8. **Design tokens + light theme** via CSS custom properties; honor `prefers-color-scheme`.
9. Real `role="tab"` semantics on `.panel-tabs`, focus-visible outlines, contrast pass.
10. Replace `ScriptProcessorNode` with `AudioWorkletNode`; show visible error on STT/mic failure.
11. Patch browser + undo (minimal: last-N patches list, revert button).
12. Tooltips on knob abbreviations; rationale visible by default; agent-diff summary line under each assistant bubble.

---

## Product Strategy Review

### Vision Clarity
Crisp and emotionally resonant: *"describe sound in natural language, get playable patch, iterate."* README hook line survives a pitch deck. **But vision/state gap borders on misleading**: `getting-started.md` reads like shipped product docs (install, DAW compat, troubleshooting, vocabulary guide) while README admits "early scaffold" with empty JUCE submodule. Two audiences (future users and current contributors) without labels.

### Scope Risk — Dominant Strategic Risk
- C++20/JUCE audio engine (DSP, voicing, MPE, plugin lifecycle)
- VST3 + AU + Standalone (three formats, two OS toolchains minimum)
- React/Vite companion UI with separate-process IPC
- Agent layer (GrammarSampler GBNF, SemanticMapper embeddings)
- Local LLM inference + two-box LAN topology (Framework Desktop + Mac Mini)
- Whisper.cpp STT
- Telemetry, session history, preset browser, debug dashboard

Each is 3–6 months on its own. For solo/small-team OSS this is ~5 years serially. The 28 test files are a leading indicator of premature breadth, not depth.

### Differentiation
- **vs Synplant 2 (Genopatch)**: audio-in → patch. Agentic Synth is text-in → patch. Different modality.
- **vs Arturia Pigments AI / Output Co-Producer**: cloud-backed, closed, subscription. Wedge: **local-first + open source + own-your-model**.
- **Real moat candidates**: GBNF-constrained generation guaranteeing valid patches + own-the-model story. Two-box LAN topology is a tinkerer's dream but mainstream non-starter.
- **Target user is unspecified.** Bedroom producer on a MacBook ≠ enthusiast with a $2k Framework Desktop. Pick one.

### Roadmap Read
No explicit roadmap file. Phase1/2/3 test naming implies an internal phasing but not externalized. Inferred:
- **P1:** agent bridge + grammar-constrained generation against local llama.cpp (mostly scaffolded)
- **P2:** JUCE plugin wiring, real DSP voice, React UI integration
- **P3:** STT, telemetry, preset browser, DAW polish, distribution/signing

**Order is wrong.** P2 should be P1. A playable synth with hardcoded patches beats a perfect agent emitting JSON into the void.

### Strategic Recommendations
1. **Pick one wedge for v0.1.** Recommend: **macOS Standalone + AU only, Ableton/Logic on M-series Mac, single-box local inference** (Phi-4 14B or Llama 3.2 3B). Defer VST3, Windows, Linux, two-box LAN, STT.
2. **Reorder: ship a playable JUCE synth with 5 hand-authored patches BEFORE agent integration.** De-risks the hardest path (audio quality) and gives every demo a working sound.
3. **Split docs into `docs/users/` (future, DRAFT-labeled) and `docs/contributors/` (current truth).**
4. **One north-star metric**: time-from-prompt-to-satisfying-sound (target < 30 s including LLM latency).
5. **MIT is correct.** Don't optimize for monetization — optimize for a working demo video.
6. **Write an ADR**: "Single-box vs two-box inference." Two-box narrows addressable user base by ~99%; make it explicit.

---

## DevOps Review

### Strengths
- `concurrency` group with `cancel-in-progress` (`.github/workflows/ci.yml:9-11`).
- Node `setup-node@v4` with `cache: npm` keyed on `ui/package-lock.json` (lockfile committed).
- Catch2 pinned to `v3.7.1` via FetchContent (`tests/CMakeLists.txt:6`).
- `CMakePresets.json` covers Linux/macOS-universal/Windows-MSVC; `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`; arm64+x86_64 fat-binary preset.
- Pre-commit (`.pre-commit-config.yaml`) enforces clang-format v19.1.7, ESLint, commitlint.
- `tests` job depends on `lint` — fail-fast.
- `set -euo pipefail` in `scripts/*.sh`.

### Critical Gaps
- **JUCE submodule empty** (`git submodule status` shows leading `-` on `third_party/JUCE`). Fresh clone fails at `CMakeLists.txt:17`. Document mandatory `git clone --recursive`.
- **llama.cpp is NOT a submodule.** `.gitmodules` declares only JUCE; `third_party/llama.cpp/` is a vendored copy with no version pin.
- **No plugin build job in CI.** Workflow only lints + runs tests with `AGENTIC_SYNTH_BUILD_PLUGIN=OFF` (`ci.yml:88-89`). VST3/AU/Standalone never compiled in CI on any platform.
- **No ccache / CMake build cache.** From-scratch every run.
- **No pluginval / auval in CI.** `scripts/plugin-validation.sh` exists, never invoked.
- **`scripts/download-models.sh` SHA256s are placeholder zeros** (l.16, 20) and skip verification when "no hash configured" — silent supply-chain hole.
- **No `npm audit` integrity check.**

### Self-Hosted Runner Risks
- Both jobs hard-pin to `[self-hosted, Linux, X64, unit-test]` (`ci.yml:16, 52`). 18+ hour queue → runner offline. No fallback (`ubuntu-latest`) → **CI bricked**.
- `sudo apt-get install` mid-job every run — slow, network-dependent.
- No runner auto-scaling, no ephemeral container isolation, no health monitoring visible.
- **Security:** Self-hosted runners executing PR code from forks is a known RCE vector. Workflow accepts `pull_request` (`ci.yml:6`) — if outside PRs are ever accepted, the runner is exposed.

### Release Engineering Gaps
- No release workflow, no tag triggers, no GH Release artifacts.
- No macOS code signing / notarization (no `codesign`, `productsign`, `notarytool`).
- No Windows codesigning (`signtool`).
- No installer packaging (`pkgbuild`/`productbuild`/WiX/NSIS).
- No SBOM / SLSA provenance. No GGUF model distribution story.
- No version-bump automation; `project(... VERSION 0.1.0)` hand-edited.

### Recommendations (prioritized)
1. **Unblock CI now.** Swap `runs-on` in `ci.yml:16, 52` to `ubuntu-latest`; keep self-hosted as a matrix entry only for heavy plugin builds. Document runner host + recovery runbook.
2. **Fix submodules.** Initialise `third_party/JUCE`; either add llama.cpp to `.gitmodules` with a pinned tag or record SHA in `third_party/llama.cpp/VERSION`.
3. **Add plugin build job.** Matrix `[ubuntu-latest, macos-14, windows-latest]` with `AGENTIC_SYNTH_BUILD_PLUGIN=ON`, `hendrikmuhs/ccache-action`, `actions/upload-artifact@v4` for VST3/AU/Standalone.
4. **Wire pluginval/auval** from the macOS build job.
5. **Lock dependencies.** Same clang-format version in CI as pre-commit; CMake `3.28.x` (already done); `.nvmrc` for Node 20.
6. **Real SHA256s** in `scripts/download-models.sh:16,20` — fail closed.
7. **Release workflow.** `on: push: tags: ['v*']` → matrix build → codesign + `notarytool` → `softprops/action-gh-release@v2`.
8. **Branch protection** once green: require `Lint` + `Tests`, require PR review, disallow force-push.
9. **Restrict `pull_request` on self-hosted** (`pull_request_target` with allow-list, or fork guard).
10. **`ui/dist/`** is currently committed — `.gitignore` it and build in CI.

---

## SRE / Reliability Review

### Reliability Risks (ranked)

1. **Port collision on multi-instance DAW use** — `WebSocketBridge::start(int port = 9002)` calls `createListener(port_)` and on failure returns from `run()` silently. Opening 4 plugin instances = 3 silent dead bridges, UI hangs on connect with no diagnostic. Same for `GenerationService` hardcoded `50051`.
2. **`std::thread(...).detach()` in `GenerationService::start`** (`src/agent/GenerationService.cpp:20,101`) — DAW unloads plugin → detached thread keeps writing to freed `this`. Crash on close.
3. **Blocking socket reads** with no timeout — slow/hostile client can hold a per-client thread indefinitely.
4. **LLM call has 15s timeout but no cancellation surface.** New prompt mid-generation has no way to cancel. Network partition stalls calling thread up to 15s while UI thinks agent died.
5. **`StreamParser` silently swallows malformed input** — unknown keys ignored, no max-key-length, no field-count cap. Truncated stream leaves `partial_` permanently un-finalized with no surfaceable error.
6. **No SIGPIPE guard** on `GenerationService` write path — POSIX `write()` to a closed peer kills the process by default.
7. **Telemetry file path collision** — `defaultLogPath()` uses `getpid()`; pid reuse across DAW restarts will overwrite.

### Observability Gaps
- **No structured logs** anywhere in `src/agent/`. Field bugs unreproducible.
- **No crash handler / breadcrumb** — privacy statement says "no crash reporting", but there's no local breadcrumb either.
- **Telemetry covers latency/tokens/error_type but not**: WS connect failures, port-bind failures, LLM HTTP status codes, StreamParser truncation, queue overflow, voice steals, audio dropouts.
- **No version field** in telemetry or WS handshake → plugin/UI/LLM-server drift is silent.
- **No live health broadcast** to UI → UI can't render a degraded-mode badge.

### Failure Modes & Degradation

| Scenario | Current | Should be |
|---|---|---|
| llama.cpp down | 15s blocking timeout, empty response, no UI signal | Health probe + UI banner; fallback to heuristic |
| LAN partition (two-box) | Same as above | Detect via `/health` ping, switch to local heuristic |
| Port 9002 in use | Silent bridge death | Probe 9002→9011, advertise chosen port |
| 4 plugin instances | Only first wins port | Per-instance ephemeral port + discovery file |
| Malformed LLM JSON | Partial patch applied, no completion | Watchdog: if no `done_` within N seconds, revert |
| UI ws disconnects mid-gen | Tokens still streamed to dead client | Track per-client gen state, cancel on disconnect |

### Proposed SLIs/SLOs

| SLI | Target | Window |
|---|---|---|
| Time-to-first-token | p95 < 800 ms | 7d |
| Time-to-first-audible-change | p95 < 500 ms | 7d |
| Full patch completion success | 99% (non-net) / 95% (net included) | 7d |
| Audio dropouts attributable to plugin | < 0.01 xruns/min | 24h |
| WS bridge availability per instance | 99.9% of session | session |
| LLM HTTP error rate | < 1% | 7d |

Error-budget burn → auto-disable speculative decoding / fall back to heuristic-only.

### Recommendations
1. **Port fallback + discovery file**: probe `9002..9011`; write `~/Library/Application Support/agentic-synth/instances/<pid>.json`.
2. **Join, don't detach** in `GenerationService`; add `stop()`; ignore SIGPIPE at startup.
3. **Surface `createListener` failure** via status callback → plugin GUI badge.
4. **Add structured logger** (file, ring-buffered, opt-in) with categories `ws/llm/parser/audio`.
5. **Health broadcast** on WS every 5 s: `{type:"health", llm:"ok|degraded|down", parser_ok, queue_depth}`.
6. **Cancellation token** on `submitPrompt`; per-client gen-id; abort on disconnect or new submit.
7. **Version handshake** as first WS message: `{plugin_version, ui_version_required, ws_protocol_version}`.
8. **Extend Telemetry**: bind failures, ws connects/disconnects, HTTP status histogram, `stream_truncated`, audio xruns.
9. **StreamParser watchdog**: if `feedChunk` hasn't closed root in T seconds, emit `parser_stall` and revert.
10. **Pre-flight `/health`** on LLM server before submit.
11. **Crash breadcrumb file** on plugin construct/destruct + last prompt — local-only.
12. **`instance_id` UUID** per plugin construct in telemetry + WS handshake.

---

## DSP / Audio Engineering Review

### Sonic Quality Assessment
Competent VA core with serious sonic gaps. MoogLadder analytic ZDF and Cytomic SVF will sound clean in the midrange. VA oscillator drift + PolyBLEP gives passable analog character at musical pitches. **But:** no filter drive/saturation in signal path, no FM despite `PatchStruct` exposing `fm_ratio`/`fm_depth`, no Pulse/Sine/Noise types despite the enum, **no reverb or delay implementation files exist**, no modulation matrix beyond LFO→nothing. PatchStruct promises an instrument the engine cannot deliver — what plays today is saw/square/triangle through a linear Moog with hard amp envelope. The parts that exist are mostly correct; the engine is **not assembled**.

### DSP Correctness Issues (ranked by audible impact)

1. **Triangle aliases.** `VAOscillator.cpp:103-114` integrates PolyBLEP square but clamps to ±1 only every 1024 samples (`triRecenterCounter_`). Integrator has no leak → DC drift + audible grit > C6. Use BLAMP residual or a leaky integrator (`triAccum *= 0.9995`).
2. **Wavetable mip selection is anti-aliased by averaging, not band-limiting.** `WavetableOscillator.cpp:30-31` decimates by pairwise mean — box filter with -13 dB stopband. Real wavetable synths build mips via FFT zero-out > Nyquist/2 per octave. Audible buzz on bright tables at high notes.
3. **No interpolation between mip levels** (`WavetableOscillator.cpp:118-122`). Integer mip changes → timbral pop at each octave. Crossfade adjacent mips.
4. **PolyBLEP square is asymmetric near phase=0.** `VAOscillator.cpp:97-99` — verify residual sign convention. Symptom: subtle DC at high freq, odd-harmonic asymmetry.
5. **MoogLadder ZDF is linear-only.** `Filter.cpp:42-70` — no `tanh` in feedback. `k` capped below 4.0 so it never self-oscillates. Clinical at high resonance. **#1 character fix.**
6. **SVF resonance cap (`Filter.cpp:105`)** `k = 2.0 - 1.9·res` → max Q≈10, no nonlinearity. Safe but boring.
7. **Envelope click on retrigger.** `ADSREnvelope.cpp:29` doesn't zero `output_`. Voice stealing in `VoiceManager.cpp:182-203` doesn't fade out previous voice — instant filter-state takeover clicks.
8. **DCBlocker coefficient.** `DCBlocker.h:10` `R = 1 - 2π·20/Fs`; correct form is `R = exp(-2π·fc/Fs)`. Approximation only valid for low fc/Fs.
9. **LFO has no smoothing on S&H** (`LFO.cpp:27-32`) — zipper noise on modulated cutoff.
10. **No a-rate vs k-rate distinction.** `VoiceManager.cpp:96-106` recomputes `g_ = tan(πfc/Fs)` per parameter event; no per-sample smoothing on any param. Cutoff knob moves zipper.

### Missing Capabilities
- **No mod matrix.** `Voice::render` never calls `lfo.processSample()` (`VoiceManager.cpp:22-34`) — entire LFO system is dead code.
- **No velocity sensitivity** (`VoiceManager.cpp:55` `float /*velocity*/`).
- **No filter envelope wiring** (interpolated by MorphEngine but never instantiated).
- **No aftertouch, no MPE, no key-tracking.**
- **No FM/PM** between oscillators.
- **No noise oscillator.**
- **No filter drive** (`FilterParams.drive` ignored).
- **No stereo path.** `renderBlock(L,R)` produces dual-mono (`VoiceManager.cpp:144-153`). No per-voice pan, no stereo width.
- **No reverb or delay implementation in codebase.**
- **No oversampling** on filter/oscillators — Moog at high res + bright source aliases.
- **No voice fade-out on steal.**

### Numerical / Stability Concerns
- **No denormal protection.** Filter integrators (`Filter.cpp:117-118, 58-67`) will denormal-stall during silence on x86 without FTZ/DAZ. Add `_MM_SET_FLUSH_ZERO_MODE` in `processBlock` or inject `+1e-20f - 1e-20f`.
- **MorphEngine linear-interpolates cutoff/rate/env-times** (`MorphEngine.cpp:76-150`) — perceptually exponential-wrong. 18000→200 Hz linear sweep accelerates at the top. Log-domain interp.
- **MorphEngine discrete-field snap at t=0.5** (`MorphEngine.cpp:91-94, 103, 123-127`) — oscillator waveform/filter type/voice count flips mid-morph = audible click. Crossfade between two engine instances or hold in one bin until commit.
- **No block-rate smoothing on PatchStruct apply.** Every user knob = 10 ms zipper on next block.

### Recommendations (prioritized by audio quality impact)

1. **Per-sample smoothing on cutoff, resonance, master_gain, osc.volume.** One-pole at 30 Hz fixes 90% of zipper. **Biggest single quality win.**
2. **Make MoogLadder nonlinear.** Wrap feedback with `std::tanh(k·y4)` (Newton 2–3 iters or `tanh` approximation). Lift `k` cap to allow self-oscillation. Single biggest sonic improvement.
3. **Voice fade-out on steal** (~5 ms ramp) — `VoiceManager.cpp:182`. Stops clicks.
4. **FFT-based band-limited mip construction** in `WavetableData::buildMipLevel` (`WavetableOscillator.cpp:22-33`).
5. **Crossfade between mip levels** (`WavetableOscillator.cpp:118`).
6. **Fix triangle integrator leakage** (`VAOscillator.cpp:107`) → `triAccum *= leakCoeff`.
7. **Wire LFOs through a mod matrix** to cutoff/pitch/amp. Currently dead.
8. **Use velocity** (`VoiceManager.cpp:55`) → amp env peak + filter env mod amount.
9. **Wire filter envelope.**
10. **Log-domain interp in `MorphEngine::lerp`** for cutoff/rate/env-times.
11. **Crossfade morph for discrete fields** instead of midpoint snap.
12. **Enable FTZ/DAZ** at audio callback top.
13. **Real stereo path** — per-voice constant-power pan, optional stereo width on detuned osc pairs.
14. **Implement reverb (FDN or Schroeder + Freeverb) + delay** with feedback clamp at 0.99 and NaN flush.
15. **DCBlocker** → `R = exp(-2π·fc/Fs)`; fc = 10 Hz.
16. **PolyBLEP square symmetry audit** (`VAOscillator.cpp:95-101`).

**Bottom line:** Building blocks compile and individually behave; engine is **not assembled**. Modulation, velocity, FX, mod-matrix, stereo, and ~half the PatchStruct fields are unhooked. Of the parts that *are* wired, linear Moog and box-filtered wavetable mips will be the two most audible quality gaps once everything is connected.

---

## Cross-Functional Synthesis — One Prioritized Plan

### Week 1 — Unbreak the build & CI
- [DevOps] Swap `runs-on` to `ubuntu-latest`; keep self-hosted as opt-in matrix entry.
- [DevOps/Arch] `FetchContent_Declare` for JUCE with pinned tag; fallback to populated `third_party/`. Pin llama.cpp SHA in `third_party/llama.cpp/VERSION`.
- [QA] Add 3 orphaned tests (`MorphEngine`, `PatchStateManager`, `SPSCQueue`) to `tests/CMakeLists.txt`.
- [DevOps] Add ccache + `actions/upload-artifact` for VST3/AU/Standalone.

### Week 2 — Truth-in-naming
- [Product] Split docs into `docs/users/` (DRAFT) and `docs/contributors/`. Rewrite README to match `src/` reality.
- [QA] Rename `Phase3AcceptanceTest` → `VoiceManagerLongRunSmokeTest`; same for `Phase2`. Acceptance suite = behavior, not unit smoke.
- [QA] Delete `(void)result;` dead assertion in `PatchStateManagerTest:54-59`.
- [Eng] Delete or finish `SynthEngine.cpp`, `WhisperClient.cpp`, `PresetDatabase.cpp` stubs.

### Week 3-4 — Make it actually play sound (P2-before-P1 reorder)
- [DSP] Wire LFOs into `Voice::render`; apply velocity; instantiate filter envelope; per-sample smoothing on cutoff/res/gain.
- [DSP] Nonlinear Moog (`tanh` feedback); voice fade-on-steal; FTZ/DAZ; stereo path; one reverb (Freeverb-style).
- [Arch/Eng] Replace `PrePatchPipeline::producerMutex_` with a true SP serialization point.
- [Eng] `juce::JSON` in `StreamParser` and `SemanticMapper`. Cache `SemanticMapper` embeddings at startup.

### Week 5-6 — Reliability & UX
- [SRE] Port fallback (9002..9011) + per-instance discovery file. Health broadcast. Join (don't detach) `GenerationService`. SIGPIPE ignore. Structured logger.
- [UX] `Knob.tsx` a11y (`role="slider"` + keyboard). Persistent agent-diff per message. Audition button. A/B commit actions.
- [Product] Pick the wedge for v0.1: **macOS Standalone + AU only, M-series Mac, single-box inference, Phi-4 14B**. Defer everything else.

### Out of scope for v0.1
VST3, Windows, Linux, two-box LAN, Whisper STT, preset DB, MPE, code signing automation, mod-matrix UI, light theme.

---

## Files Most Cited

- `src/engine/SPSCQueue.h`, `RealtimeSafety.h`, `Filter.cpp`, `MorphEngine.cpp`, `VoiceManager.cpp`, `VAOscillator.cpp`, `WavetableOscillator.cpp`, `ADSREnvelope.cpp`, `LFO.cpp`, `DCBlocker.h`, `SynthEngine.cpp`, `PatchStruct.h`, `PatchStateManager.h`
- `src/agent/AgentBridge.{h,cpp}`, `PrePatchPipeline.{h,cpp}`, `WebSocketBridge.{h,cpp}`, `GenerationService.{h,cpp}`, `StreamParser.cpp`, `Telemetry.{h,cpp}`
- `src/mapper/SemanticMapper.cpp`, `GrammarSampler.cpp`
- `src/plugin/AgenticSynthPlugin.cpp`, `src/MainComponent.cpp`
- `ui/src/App.tsx`, `App.css`, `components/{ChatInterface,KnobGrid,Knob,PatchPreview,PushToTalk}.tsx`
- `CMakeLists.txt`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `CMakePresets.json`, `.gitmodules`
- `.github/workflows/ci.yml`, `.pre-commit-config.yaml`
- `scripts/download-models.sh`, `scripts/plugin-validation.sh`
- `docs/adr/ADR-0001-initial-architecture.md`, `docs/getting-started.md`, `docs/local-inference.md`, `docs/privacy-statement.md`

---

*Generated by 8 parallel sub-agent reviews coordinated through Claude Code. Each section reflects its specialist's findings verbatim where possible.*
