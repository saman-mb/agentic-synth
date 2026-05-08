# DAW Smoke Test Procedure

## Ableton Live
1. Load AgenticSynth VST3 on a MIDI track
2. Play a C major chord — verify sound is audible
3. Tweak parameters via automation lanes
4. Save project, close, reopen — verify state restored
5. Test MIDI learn on mod wheel

## Logic Pro
1. Load AgenticSynth AU on a software instrument track
2. AU Validation should pass (via auval)
3. Play notes from the built-in keyboard
4. Test parameter automation
5. Save/restore project state

## FL Studio
1. Load VST3 in the Channel Rack
2. Draw a MIDI pattern
3. Verify parameter automation works
4. Test MIDI CC control

## Reaper
1. Load VST3 on a new track
2. Arm track, play MIDI
3. Test MIDI learn
4. Save/restore project state

## Automated Validation

```bash
# Run pluginval
pluginval --strictness-level 10 --validate /path/to/AgenticSynth.vst3

# Run auval (macOS)
auval -v aumu Vst3 Agnt
```
