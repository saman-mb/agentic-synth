# Privacy and Data-Handling Statement

**Last updated: May 2026**

Agentic Synth is designed with a **local-first** philosophy. The synthesizer engine, AI patch generation, and all audio processing run entirely on your machine.

## What We Collect

### Telemetry (Opt-In Only)

If enabled, local telemetry collects:
- Latency metrics for patch generation (P50/P95/P99)
- Error rates and types during generation
- Token usage for LLM-based patch creation
- UI interaction timing (button clicks, parameter changes)

**Telemetry data never leaves your machine.** It is stored in a local JSON log file at:

- **macOS/Linux:** `~/.local/share/agentic-synth/telemetry/`
- **Windows:** `%APPDATA%/agentic-synth/telemetry/`

You can view telemetry through the debug dashboard panel in the UI. Telemetry can be enabled/disabled at any time in Settings.

### Session History

Your chat history and generated patches are saved locally for session-aware interactions. This data:
- Stays on your machine
- Is stored in your project directory
- Can be cleared at any time

## What We Do NOT Collect

- **No audio recording** — Audio processing happens entirely in memory and is never stored or transmitted
- **No personal data** — We do not collect names, email addresses, or any personally identifiable information
- **No usage tracking** — No analytics services, no cookies, no tracking pixels
- **No network requests** — The synth does not phone home. All AI inference runs locally via llama.cpp
- **No crash reporting** — Crashes are handled locally without external reporting

## Third-Party Dependencies

| Dependency | Purpose | Data Access |
|-----------|---------|-------------|
| JUCE 7 | Audio framework | None — local only |
| llama.cpp | LLM inference on CPU | None — local only |
| Whisper.cpp | Speech-to-text | None — local only |
| Catch2 | Unit testing | None — build only |
| React + Vite | UI framework | None — local only |

None of these dependencies transmit data off your machine.

## User Control

- **Telemetry** — Enable/disable in Settings > Telemetry
- **Session data** — Clear in Settings > Data
- **All data** — Delete by removing `~/.local/share/agentic-synth/` or the application's data directory

## Contact

For privacy questions or data deletion requests, open an issue on GitHub.
