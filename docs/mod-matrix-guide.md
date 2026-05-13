# Mod Matrix — Plain Guide

The mod matrix is the wiring panel that makes things in your patch MOVE — wiggle, breathe, ramp, glide. Without it, every knob value is fixed for the whole note. With it, knobs become *destinations* that get driven by *sources* over time.

## The Mental Model

Think of it like patch cables on an old modular synth:

```
SOURCE  ────────►  DESTINATION   (with an AMOUNT dial)

  LFO1  ────────►  Filter Cutoff   ±0.6
  ENV2  ────────►  Pitch           +0.1
  MACRO1 ───────►  Reverb Mix      +0.4
```

A SOURCE is something that produces a moving value over time.
A DESTINATION is any knob in the synth.
The AMOUNT controls how strongly the source pushes the destination.

That's the whole concept.

## The Six Sources

There are 10 sources, but you only ever interact with 6 categories:

| Source | What it does | Use it for |
|---|---|---|
| **LFO1, LFO2** | Repeating wave (sine/triangle/saw/square/random) at a chosen rate | Vibrato, tremolo, filter wobble, slow drift, wide pan motion |
| **ENV1, ENV2** | One-shot shape on every note (attack/decay/sustain/release) | Filter sweeps on note-on, pitch sweeps for risers, modulation that fades over the note |
| **MACRO 1–4** | A knob YOU control at the top of the synth | "Brightness," "tension," "air" — performance handles that drive several params at once |
| **VELOCITY** | How hard you played the note (0–127 from MIDI) | Harder = brighter / louder / faster attack |
| **KEYTRACK** | Which note you played (low/high) | Bass-stable + treble-bright filter that follows the keyboard |
| **MOD WHEEL / AFTERTOUCH** | (Live MIDI controllers — not surfaced today; the agent uses these mostly) | Expression on stage |

## The Three Destinations You Probably Want

Anywhere you see a knob, you can route a source to it. The most musically valuable destinations:

- **Filter Cutoff** — the brightness control. Modulating this is 80% of synth motion.
- **Pitch** — vibrato, pitch sweeps, glide-into-note feel.
- **Amplitude** — tremolo, ducking, swells.
- **Wavetable Position** — slow timbre morph on wavetable osc.
- **FM Ratio** — bell shimmer, evolving FM tones.
- **Pan** — auto-panning width.

## How to Use It (Three Ways)

### 1. Let the agent do it (default + recommended)

Generated patches arrive with a mod matrix already wired. The 4 macros get NAMED (BRIGHTNESS / DRIFT / GRIP / AIR) and assigned routes that suit the patch character. You just play.

### 2. Drag a source onto a knob

In the **MOD SOURCES** strip above the synth (the colored dots labeled L1, L2, E1, E2, etc.), drag any dot and drop it on any knob in the modules area. A glowing ring of the source's color appears around that knob — it's now wired. Drag again to assign more sources or higher depths.

### 3. Use the Modulation Matrix list panel

Below the synth modules, there's a `▸ MODULATION MATRIX` drawer. Open it to see a list of all current connections, with sliders for amount + an enable toggle + a delete X. This is the power-user view — useful for editing depths precisely or removing a connection you don't want.

You can also click **CONSTELLATION** in that drawer for a visual map: dots on the left are sources, dots on the right are destinations, lines between them flow when active.

## What Each Sliding Number Means

When you see `+0.6` on a connection:
- The source value (e.g. LFO1 at 100% positive peak = +1.0) gets multiplied by 0.6
- That product is ADDED to the knob's base value
- So a knob at 0.5 with a +0.6 LFO connection swings between 0.5−0.6 and 0.5+0.6 (clamped to the knob's legal range)

NEGATIVE amounts invert the source. Useful trick: -0.5 on filter cutoff means as the LFO goes UP, the filter goes DOWN. Inverse routing.

## Common Recipes

**"Make this filter breathe"**
- LFO1 (sine, 0.3 Hz) → Filter Cutoff at +0.4
- Slow filter drift, sounds alive.

**"Make this lead vibrato"**
- LFO1 (sine, 5 Hz) → Pitch at +0.04
- Tiny depth on pitch + medium rate = vocal-style vibrato.

**"Make velocity control brightness"**
- VELOCITY → Filter Cutoff at +0.5
- Harder hit = brighter sound. Standard expressive trick.

**"Make filter close as the note fades"**
- AmpEnv (or ENV2) → Filter Cutoff at −0.6 (note the NEGATIVE)
- As the envelope drops, the filter closes too. Natural-sounding decay.

**"Create a riser"**
- ENV2 with slow attack (3 sec) → Filter Cutoff at +0.9
- Hold a chord → filter sweeps open over 3 seconds.

**"Performance brightness macro"**
- Macro 1 → Filter Cutoff at +0.7
- Macro 1 → Reverb Mix at +0.3
- Macro 1 → Drive at +0.2
- Now ONE knob ("BRIGHTNESS") makes the patch open up + push out + dirty up together.

## Common Confusions Answered

**"Why are there macro knobs AND mod sources in the same strip?"**
- The 4 large macros at the top are *user-facing* — you turn them by hand.
- The 6 small dots (L1/L2/E1/E2/Vel/Key) are *automatic* — they move on their own.
- Both can drive any knob via the matrix. Macros = your hand. Sources = the patch's life of its own.

**"Why can the matrix have unlimited rows when there are only 4 macros / 6 sources?"**
- One source can route to MANY destinations.
- Example: LFO1 can wobble Filter Cutoff AND tremolo Amplitude AND drift Pan, all at the same time. Each is one row.
- 4 macros × 4 routes + 2 LFOs × 4 routes = 24 connections, easily.

**"Do I have to edit the matrix manually?"**
- No. The agent wires up modulation when it generates a patch. You only touch the matrix if you want to add/remove/tweak.
- The macros are the *exception* — designed to be twisted by hand during play.

**"What happens if two sources hit the same destination?"**
- Their outputs add together. So `LFO1 +0.3` + `Velocity +0.4` on the cutoff = both push, sometimes the same way, sometimes opposite. Sum gets clamped to the knob's range.

## The 5-Second Cheat Sheet

1. Generate a patch — modulation is already wired.
2. Twist macros while playing — that's the live performance layer.
3. Want more wobble? Drag LFO1 onto cutoff. Done.
4. Want less? Open the matrix drawer + lower the amount, or delete the row.
5. Forget what's wired? Open the Constellation view.

That's it. Mod matrix is just: "this thing moves that thing, by this much."
