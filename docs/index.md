# Tambra Documentation

Describe a sound in plain English, get a playable synth patch.
Start at the [README](../README.md) if you haven't built the project yet.

---

## 🎹 I want to use it

| Guide | What it covers |
|---|---|
| [Getting Started](getting-started.md) | Install, launch, and create your first patch |
| [Vocabulary Guide](getting-started.md#vocabulary-guide) | Which words the agent understands, and what they do to the sound |
| [Timbre Profile Map](timbre-profile-map.md) | The full sound-design range — oscillators, filters, envelopes, modulation, effects |
| [Mod Matrix Guide](mod-matrix-guide.md) | Routing modulation sources to destinations |
| [Troubleshooting](getting-started.md#troubleshooting) | Common problems and fixes |

> [!TIP]
> **No sound?** Make sure you are running the plugin-format standalone
> (`AgenticSynth_Plugin_Standalone`), not the `AgenticSynth` target — the
> latter is a UI-only shell with no audio device. Then check
> **Settings → Audio device → Open** for your output device.

---

## 🔧 I want to work on it

| Guide | What it covers |
|---|---|
| [Architecture](architecture.md) | System design, component boundaries, how the agent and engine talk |
| [Audio Engine](audio-engine.md) | Signal flow, the patch contract, and DSP implementation |
| [Mobile UX & design](mobile/README.md) | Single-screen mobile UX: IA/FSM, macros, input, dark tokens + art direction (visualizer/knobs) |
| [Build & Release](build-release.md) | Compiling from source, packaging, signing |
| [Local Inference](local-inference.md) | Running LLM inference on your own hardware instead of a hosted API |
| [Contributing](../CONTRIBUTING.md) | Workflow, coding standards, commit conventions |
| [Code of Conduct](../CODE_OF_CONDUCT.md) | Community expectations |

### Architecture decisions

Design decisions and their rationale live in [`adr/`](adr/). Start with
[ADR-0008](adr/ADR-0008-nx-workspace-boundaries.md) for the Nx graph. Read these
before proposing a structural change — most of the "why is it like this?"
questions are answered there.

---

## 🧪 Testing

| Guide | What it covers |
|---|---|
| [DAW Smoke Tests](daw-smoke-test.md) | Manual verification checklist across major DAWs |

The automated suite is Catch2, run with `ctest --test-dir build`. One test
(`osc0_enabled toggle changes audio`) is a known pre-existing failure — see
Known issues in the [README](../README.md#-known-issues).

---

## 🔒 Compliance

| Guide | What it covers |
|---|---|
| [Privacy Statement](privacy-statement.md) | Mic, prompts → Gemini, web demo / mobile entitlement, deletion contact |
| [App Store / Play nutrition labels](mobile/privacy-nutrition-labels.md) | Paste-ready labels aligned with the privacy statement |
| [Naming (TIMBRE → Tambra)](REBRAND.md) | Product / company / reserved-name decision log |

---

## Open questions and research

[`issues/`](issues/) holds written-up research questions and open problems that
are larger than a GitHub issue — useful if you're looking for something
substantial to dig into.
