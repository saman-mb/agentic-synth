# Agentic Synth

An AI-powered synthesizer that turns natural language descriptions into rich, playable sounds.

## Getting Started

### What is Agentic Synth?

Agentic Synth is a software synthesizer that accepts **natural language descriptions** ("warm analog pad with slow attack") and translates them into synthesizer parameters. It runs as both a standalone application and a VST3/AU plugin in your DAW.

### Quick Start

1. **Install** — Download the latest release for your platform
2. **Launch** — Open the standalone app or load the plugin in your DAW
3. **Describe** — Type "deep sub bass with slight grit" into the chat panel
4. **Play** — The synth generates a patch instantly. Tweak with knobs or refine with more text

### Interface Overview

- **Chat panel** — Left side. Type NL descriptions, see agent responses
- **Knob grid** — Center. Visual parameters with real-time feedback
- **Keyboard** — Bottom. Play notes, test the sound
- **Preset browser** — Save/load patches

### First Patch

Try these prompts:
- "warm pad with slow attack, gentle filter sweep"
- "aggressive lead with fast decay and lots of resonance"
- "sub bass, clean sine with subtle pitch modulation"

### System Requirements

- macOS 12+, Windows 10+, or Linux (x86_64)
- Audio interface (built-in works)
- 4GB RAM minimum, 8GB recommended
- DAW: Ableton Live 11+, Logic Pro 10.7+, FL Studio 21+, Reaper 6+

## Vocabulary Guide

### Oscillator Types

| Term | Meaning |
|------|---------|
| **Saw** | Bright, buzzy — rich in harmonics |
| **Square** | Hollow, reedy — odd harmonics only |
| **Triangle** | Soft, mellow — gentle harmonic roll-off |
| **Sine** | Pure, clean — fundamental only |
| **Wavetable** | Animated, evolving — waveshapes scanned through a table |

### Filter Modes

| Term | Meaning |
|------|---------|
| **LP (Low-pass)** | Removes highs, darkens sound |
| **HP (High-pass)** | Removes lows, thins sound |
| **BP (Band-pass)** | Passes a frequency band |
| **Notch** | Cuts a frequency band |
| **Moog-ladder** | Classic 24dB/oct low-pass with resonance character |

### Envelope Stages (ADSR)

- **Attack** — Time to reach peak level
- **Decay** — Time to fall to sustain level
- **Sustain** — Level held while key is pressed
- **Release** — Time to fade after key release

### LFO Shapes

- **Sine** — Smooth cyclical modulation
- **Triangle** — Smooth up-down pattern
- **Saw** — Ramp up, snap back
- **Square** — Stepped, binary modulation
- **S+H** — Random stepped values

### Modulation Targets

Common modulation destinations: pitch, filter cutoff, filter resonance, amplitude, wavetable position, pan, LFO rate

## Troubleshooting

### No Audio
- Check your audio device is selected in settings
- Verify MIDI keyboard/controller is connected
- In DAW: check the track is armed for recording

### High Latency
- Reduce buffer size in audio settings (128 or 256 samples)
- Close other CPU-heavy applications
- Disable unused oscillator voices

### Plugin Not Found
- Re-scan plugins in your DAW
- Verify the plugin file is in the correct folder
- Check platform compatibility (VST3 for most DAWs, AU for Logic)

### Patch Doesn't Sound Right
- Try more descriptive language (add "aggressive", "warm", "bright")
- Use the knob grid to adjust parameters
- Check that modulation is applied to the intended target

### Build/Compile Errors
- Ensure JUCE 7 submodule is initialized: `git submodule update --init --recursive`
- CMake 3.28+ required
- See `docs/build-release.md` for platform-specific instructions

---

*For more help, open an issue on GitHub or check the project README.*
