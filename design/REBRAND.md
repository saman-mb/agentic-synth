# TIMBRE — Rebrand + Redesign

Synthesis of 6-agent design panel (Brand Guardian, UX Researcher, UI Designer, UX Architect, Visual Storyteller, Whimsy Injector).

---

## 1. Brand

**Name**: `TIMBRE` (stylized `timbre.` or `TIMBRE//`)
**Tagline**: *Say it. Hear it.*
**Essence**: The first synthesizer you speak to — and it speaks back in sound.

**Why TIMBRE**: owns the most loaded word in sound design. Globally pronounceable. Single-word, sits next to Vital/Pigments/Serum. "Agentic" is 2024 AI-discourse vocabulary — dated within 18 months. Musicians buy instruments, not agents.

**Alts**: `LARYNX` (transgressive, anatomical) or `LUMEN` (safe, premium).

**Voice attributes**:
- Sonic-poetic — *"Describe the sound you can almost hear."*
- Confident, never technical-for-its-own-sake — button reads *"Warmer."* not *"+2dB low-shelf"*
- Quietly intelligent — *"I heard 'underwater cathedral.' Try this."*
- Tactile — *"Pull the texture toward glass."*
- Reverent of craft — *"Built for people who still believe in patches."*

**Positioning**: For producers and sound designers who think in feelings before parameters, **TIMBRE** is the AI-native software synthesizer that turns plain language and voice into fully editable patches, because it's the first synth designed around describing sound, not adjusting it.

**Pitfalls to avoid**: tech-startup blue, "powered by AI" badges, sparkle icons, skeuomorphic chrome knobs, naming the AI a character, bright friendly tone (boutique synth = austere craft).

---

## 2. Color System (dark default)

```
--bg-void:     #0A0B0F   // deepest panel
--bg-panel:    #14161D   // main module surfaces
--bg-raised:   #1E2129   // knob plates, cards
--bg-inset:    #07080B   // recessed wells (scopes)

--accent-primary:   #7C4DFF   // electric violet — signature
--accent-secondary: #FF3D88   // hot magenta — AI states, primary CTA
--accent-glow:      rgba(124,77,255,0.45)

--text-primary:   #F5F7FA   (96% white — never pure white)
--text-secondary: #A8B0BD
--text-tertiary:  #6B7280
--text-accent:    #B388FF
```

**Modulation source palette** (Pigments-style multi-hue routing):
- LFO1 cyan `#00E5FF`, LFO2 aqua-green `#00FFAA`
- ENV1 amber `#FFB300`, ENV2 orange `#FF6B35`
- Macro1 violet-pink `#E040FB`, Macro2 lime `#76FF03`
- Velocity white, Keytrack `#B0BEC5`

**Forbidden zone**: Slack-purple, Stripe-indigo, Linear-gradient-rainbow, ChatGPT-green.

---

## 3. Typography

| Use | Font | Weight | Size |
|---|---|---|---|
| App header | Söhne / Inter Display | 600 | 18px |
| Section header | Inter | 600 | 11px UPPER 0.08em |
| Param label | Inter | 500 | 10px |
| Param value | JetBrains Mono | 500 | 11px |
| Knob center value | JetBrains Mono | 600 | 13px |
| AI prompt | Söhne | 400 | 15px |

---

## 4. Knob Anatomy

**Sizes**: Large 72px (macros, master), Medium 52px (osc/filter primary), Small 36px (mod amount).

**Layered paint**:
1. Outer ring 2px stroke, full 270° arc 7→5 o'clock
2. Fill arc violet gradient (violet → lavender)
3. Inner plate radial gradient `#1E2129 → #14161D` with 1px inset top + 1px highlight bottom
4. Indicator 2px white line, 4px violet glow
5. Bipolar: arc from 12 o'clock; negative = magenta, positive = violet
6. Mod-active: concentric outer ring in mod source color, animated stroke-dasharray, opacity oscillates with mod waveform
7. Mod amount halo: 4px translucent ring at 35% in source color

**Interaction**: vertical drag, double-click reset, shift = fine, alt = bipolar, right-click context menu (Type value / MIDI learn / Assign mod). Drag colored source dot onto knob to assign modulation (Pigments/Vital pattern).

---

## 5. Layout (1280×800 default, resizable, aspect-anchored)

```
+--------------------------------------------------------------------------------+
|  LOGO   [Preset v]  < >  [A|B]  [Undo|Redo]      CPU 4% | MIDI o | OUT -6 |  ← TopBar (48px)
+--------------------------------------------------------------------------------+
| PRESETS |   OSC 1  |  OSC 2  |  OSC 3   |   ~~~ oscilloscope / spectrum ~~~  |
|  search |   knobs  |  knobs  |  knobs   |   (toggle: scope|spec|XY|wt)       |
|  [tags] +---------+---------+----------+                                     |
|  ------ |   FILTER          |  AMP ENV |                                     |
|  Bass   |   cutoff res drv  |  ADSR    |                                     |
|  Lead   |   type slope                  +---------------------------------+  |
|  Pad    +-------------------+----------+|   AI PROMPT (always-visible)    |  |
|  Pluck  |  ENV 2  | LFO 1   |  LFO 2   ||  > "describe a sound..."        |  |
|  FX     |  ADSR   | rate sh |  rate sh ||   [send] [voice] [history]      |  |
|         +---------+---------+----------+|   suggestions: bass | brighter  |  |
|         |   FX: REVERB     |  DELAY    |+---------------------------------+  |
|         |   size mix tone  |  time fb  |    MOD MATRIX (collapsible)         |
|         |                  |           |    src → dst   amt   curve  [+]    |
| (240px) |                  |           |                                     |
+---------+--------+---------+-----------+-------------------------------------+
|             [================ MIDI keyboard (88px) =================]         |
+--------------------------------------------------------------------------------+
```

**Key wins vs current**:
- Visualizer = the missing hero (top-right ~420×260, replaces empty void)
- Single dense page (no Easy/Advanced toggle — AI prompt IS easy mode)
- Knob real estate doubles
- Preset browser + tags + audition (Splice-style) — was absent
- Mod matrix permanent collapsible drawer (drag-to-assign primary)
- 4 macro knobs (TBD: above master area)
- A/B compare, undo/redo, A→B copy on TopBar
- MIDI keyboard always available

**AI integration**: prompt always-visible dock (the differentiator stays visible) + Cmd+K hotkey to focus from anywhere. Voice goes INSIDE the bar, not as a separate giant button.

---

## 6. Component Library

- **Primary button**: violet bg, white text, 8px radius, glow on hover, 0.98 press scale
- **Secondary**: raised bg, 1px border, hover border violet
- **Toggle**: 32×18 pill, violet on, spring 180ms
- **Tabs (segmented control)**: 28px, inset track, active = raised with violet bottom border
- **Slider**: 4px track, violet gradient fill, 14px circle thumb (matches knob plate)
- **Text input**: inset bg, focus violet border + 2px glow
- **AI prompt bar** (hero): 48px tall, raised bg, 12px radius, inner gradient border that slow-sweeps violet→magenta when idle, magenta pulse when generating

---

## 7. Material Language — "Synthetic Glass"

Flat-leaning with surgical depth cues. NOT glassmorphism (overdone), NOT flat (cold), NOT skeuomorphic (dated).

- Panels: solid fill + 1px hairline top-edge highlight `rgba(255,255,255,0.04)`
- Inset wells: `inset 0 1px 0 rgba(0,0,0,0.6), inset 0 -1px 0 rgba(255,255,255,0.03)`
- Raised cards: `0 1px 0 rgba(255,255,255,0.04), 0 8px 24px rgba(0,0,0,0.4)`
- No blur (readable + GPU-cheap)
- Active module: 1px violet border 30% + faint outer bloom (FabFilter "selected" clarity)

---

## 8. Visualization

- **Oscilloscope**: 1.5px violet stroke, additive blend, 6px glow, 200ms decay trail. Grid `rgba(255,255,255,0.03)`
- **Spectrum**: gradient violet→magenta→white tips, peak hold line white 60%
- **Envelope curves**: 2px violet stroke, draggable 8px white handles with glow, 8% violet fill
- **LFO shapes**: stroked in assigned mod color (cyan for LFO1, aqua for LFO2), animated playhead dot

---

## 9. Motion Language — "Considered Breath"

- **Easing**: `cubic-bezier(0.22, 0.61, 0.36, 1)` — gentle entry, decisive settle, no bounces
- **Durations**: micro 180ms, state 320ms, patch load 1.1s, boot 1.8s
- **Choreography**: stagger everything 40–80ms. Nothing arrives at the same time. (Makes Pigments cinematic vs Serum mechanical.)
- **Signature flourish**: 1-pixel cyan underline sweep on every confirmation. Apple has the spring, Linear has the fade, **TIMBRE has the sweep**.

---

## 10. Opening Moment (boot, 1.8s)

Black canvas, dead silent. Single 1px dim cyan line appears centered. At 400ms it inhales: bends into sine → square → wavetable, morphing 6 shapes in 900ms with chromatic-aberration trail. Wordmark **TIMBRE** types char-by-char (40ms intervals), caret blinks 200ms post-final, dissolves. Waveform expands outward and becomes the live oscilloscope.

---

## 11. AI Prompt as Theatre

User types *"dark sub bass"*, hits return:

1. 0–200ms: prompt dims, cyan underline sweeps L→R ("reading")
2. 200–600ms: oscilloscope flatlines + dims to 30%. Agent reasoning ticker fades in stage-right, monospaced: *"low fundamental… 55Hz… soft saturation… long release…"* (telegraph, not chatbot)
3. 600–1100ms: knobs animate to new positions, staggered spring physics, 60ms offsets in signal-flow order (OSC → FILTER → AMP → FX)
4. 1100–1400ms: oscilloscope blooms back to full, new waveform, gradient flare across trace
5. 1400ms: 800ms auto-preview sub note fading out

User hasn't touched keyboard. Room has already shifted.

---

## 12. Three "Wow" Moments

1. **Wavetable Aurora** — sweeping wavetable position fills oscilloscope background with slow-moving aurora drawn from harmonic content (high harmonics → magenta, low → cyan). The room reacts, not just the trace.
2. **Modulation Constellation** — opening mod matrix is NOT a grid. 3D constellation: sources as bright nodes, destinations dim, luminous threads pulsing with LFO rates. Drag-to-connect draws a line of light between stars.
3. **Voice Bloom** — every voice triggered renders faint particle bloom at corresponding pitch on oscilloscope X-axis. Chord = 4 blooms. Sustain = blooms slowly orbit.

---

## 13. Empty State

No patch, no MIDI input: oscilloscope shows generative Perlin-noise ambient drift (one cycle per 12s). Below prompt: 3 patch cards cycle in/out (*"Lunar Sub" — "Glass Choir" — "Acid Daydream"*). Hover any card → oscilloscope adopts that patch's waveform as preview. **Canvas never sleeps.**

---

## 14. Five Signature Micro-Interactions

a. **Patch-load knob settle**: spring physics 250ms, staggered 60ms in signal-flow order. Eye watches patch assemble itself.

b. **Value tooltip momentum**: numeric readout floats above cursor with 2-frame lag + horizontal lean. Settles with tiny overshoot.

c. **Section "fold"**: collapse via CSS 3D rotateX-on-bottom-edge (paper-fold), 220ms ease-out. Not standard accordion.

d. **Muted badge breathing**: grey pill opacity pulses 0.5↔0.7 over 4s. Synth feels waiting, not switched off.

e. **Telemetry tab heartbeat**: new events = slow leftward sweep under tab label (like radar). Reading clears.

---

## 15. AI-Specific Delights

- **Prompt shimmer while thinking**: submitted text gains slow L→R gradient sweep (violet → white → violet, 3s cycle). Stops when first knob moves — shimmer physically hands off to the patch.
- **Agent reasoning ghost**: faint 1px ring traces target knob ~200ms before it animates there. Agent "points" before "speaking."
- **Voice → wavetable**: hold-to-speak mic waveform draws across bottom strip. On release, deforms into OSC1 wavetable preview shape. Your voice becomes the sound source visually.
- **Inline patch diff**: knobs the agent moved show 1px violet tick at previous position for 6 seconds post-generation. Hover: *"was: 0.42 → now: 0.78"*.

---

## 16. Easter Eggs

- Type `sudo make me a sound` → agent responds in 80s green terminal font, generates gnarly FM patch labeled "RTFM"
- Hold Option + double-click logo → all knobs do one synchronized 360° rotation, return. Once per session.

---

## 17. UI Sound (mostly silent)

Two exceptions, both user-toggleable:
- **Voice-transcribe confirmation**: 40ms sine pip at patch's current root note, –30dBFS, through plugin output bus. Confirmation USES your sound. Skip if transport playing.
- **Patch load complete**: soft tape-stop "thunk" after settle finishes. Off by default.

Nothing on hover/click/drag/open — those compete with audio work.

---

## 18. Failure States (Brand-Defining)

Current: `Microphone unavailable (TypeError).` ← engineering leak. **Replace**:

- **Mic unavailable**: speak button becomes struck-through mic icon, subtitle *"Can't hear you — check mic permissions in System Settings."* + inline "Open Settings" link. Calm grey, not red.
- **LLM network error**: prompt input KEEPS user's typed text. Below: *"Couldn't reach the agent. Trying again in 4… 3… 2…"* + manual "Retry now". Auto-retries twice silent first.
- **Invalid/empty prompt**: NO error. Placeholder dims, submit arrow shakes head (180ms horizontal nudge). Wordless.

**Rule**: brand never apologizes, never jokes, never blames user. States the situation + next action.

---

## 19. Critical Missing Features (v1 must-have for "class-leading")

1. **Preset browser** — tags, audition, favorites, init patch, search
2. **Oscilloscope + spectrum analyzer** (always-visible, post-amp)
3. **Envelope curve display** with draggable handles (Pigments/Operator pattern)
4. **Modulation matrix view** + inline modulation rings on knobs
5. **A/B compare** + visible undo stack with named states
6. **4 macro knobs** (assignable, renamable)
7. **MIDI keyboard** at bottom

---

## 20. Marketing-Grade Screenshots

- **Product portrait**: symmetric, oscilloscope at full bloom with sustained chord, hero knob positions (Cutoff 70%, Reso 40%), prompt empty, pure `#0A0E14` background, retina specular highlights
- **In flow**: asymmetric, mod constellation open with 3 pulsing threads, oscilloscope mid-sweep, prompt mid-type *"warm tape-saturated rh|"*, one knob caught with motion-blur arc
- **Detail**: macro on oscilloscope, 80% crop, chromatic-aberration fringing, knobs at frame bottom soft-focus bokeh — looks like a photograph of a real instrument

---

## Risk: Wordmark First

> Brief the type designer before the UI designer — the wordmark sets the ceiling for everything else. — Brand Guardian

Current "Agentic Synth" wordmark in generic geometric sans = #1 reason product looks like a prototype. Custom letterforms or heavily customized type treatment for TIMBRE is non-negotiable.
