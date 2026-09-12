# Mobile art direction — dark instrument

> **v1/proto** · epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Part of [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Feeds E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294)

Look-and-feel for Tambra mobile: one dark canvas, four macro knobs, full-width visualizer. Tokens: [`tokens.json`](./tokens.json) · contrast: [`tokens.md`](./tokens.md) · IA chrome: [`ia.md`](./ia.md) · knob **interaction** (authoritative): [`macros.md`](./macros.md).

**Bar:** iterative prototype; **decent mobile experience**, not perfection. Captain proto authorization clears this art gate for v1; refine later.

## Mood

- **Instrument first** — recessed plates, quiet chrome, audible feedback over decorative UI.
- **Dark by default** — void / canvas / panel stack from [`tokens.json`](./tokens.json); no light theme in v1.
- **Warm copper signature** — `accent.primary` for focus, macro arcs, primary CTAs (not purple-on-white AI chrome).
- **Cool viz** — `accent.viz` for motion in `el.visualizer` so shape/knobs stay warm and the sound plane stays cool.
- Brand cues from `design/REBRAND.md` (dark craft, tactile copy) adapted for **Tambra** mobile; desktop violet signature is **not** required on mobile v1.

## Composition (ties to IA)

| Chrome | Art role |
|---|---|
| `el.brand` | Hero-level wordmark; quiet metal/warm type — not a pill chip |
| `el.visualizer` | Dominant full-width plane; shows the sound, not a card inset |
| `el.macros` | Four equal knobs on a raised plate; thumb row |
| `el.play` / `el.input_cta` | High-contrast controls (`accent.play` / `accent.record`) |
| `el.sheet` | Single sheet; `radius.sheet` + `elevation.sheet`; body swaps by state |

One composition per viewport — not a dashboard. No floating promo badges on the visualizer.

## Knob visual design (aligns with macros.md)

**Interaction remains normative in [`macros.md`](./macros.md)** — this section only paints the control. No contradictions:

| Concern | Art / tokens | macros.md |
|---|---|---|
| Hit target | `touchTargetMinDp` **48**, preferred **56** | Min ~**48–56 dp** |
| Gesture | Visual follows **vertical drag** | Primary = vertical drag |
| Value | Continuous arc 0…1; optional mono value under label | Continuous `0…1`; no mandatory detent |
| Fine adjust | Slightly brighter ring / tighter glow in precision mode | Slower sensitivity or long-press precision |
| Count / labels | Four: Brightness, Movement, Space, Body | Same four; `macro.0`…`3` |
| Reach | Chrome row, not buried in sheet | One-thumb in `shape` with sheet peek |

### Paint layers (v1)

1. **Plate** — `bg.knobPlate` under the row; subtle `elevation.1`.
2. **Hit rect** — invisible ≥48 dp (prefer 56); visual disc ~44 dp centered inside.
3. **Track ring** — `control.knobRing`, 270° arc (7→5 o’clock).
4. **Value arc** — `control.knobArc` / `accent.primary` swept by position.
5. **Indicator** — `control.indicator` tick; optional soft `elevation.knobActive` while dragging (no neon stack).
6. **Label** — `type.scale.macroLabel` + `text.secondary` below; value uses `knobValue` mono if shown.

**Avoid:** skeuomorphic chrome metal, six knobs, per-param hood knobs, purple glow rims, contradicting drag axis.

## Visualizer aesthetic (`el.visualizer`)

| Topic | v1 direction |
|---|---|
| Layout | **Full-width** edge-to-edge under brand / above macros; not an inset card |
| Content | Time-domain or spectral energy of the **working patch** while audible; idle = low-amplitude breathing noise floor, not a blank void |
| Motion character | Calm, continuous, instrument-like — slow energy bloom, no celebratory particle fireworks |
| Color | Stroke/fill from `accent.viz` / `accent.vizGlow` on `bg.inset` well |
| States | Dim slightly in `say` when mic UI needs focus; full energy in `hear` / `shape` while playing |
| Non-goals | 3D scenes, album-art collage, lyric karaoke, FPS chase above budget |

### Performance budget (E5 / E3 consumers)

| Metric | Budget |
|---|---|
| **Target fps** | **30** (`component.visualizer.targetFps`) |
| Floor under load | **24** — if sustained below, reduce particles / FFT bins before dropping audio |
| Particle / path hint | ≤ ~120 simple primitives |
| Threading | Prefer UI raf/display link; never starve audio callback |
| Thermal | Prefer cheaper path on low-power / backgrounded audition |

Document regressions against this budget in E5; do not raise target to 60 for v1 proto.

## Motion elsewhere

Sheet / state chrome: `motion.duration.sheet` (~280 ms), standard easing. Prefer **2–3 intentional motions** (sheet rise, knob arc, viz energy) — not ambient glitter.

## Accessibility

Primary text/controls must keep documented AA pairings in [`tokens.md`](./tokens.md). Focus rings use `border.focus`. Record/mic states use `accent.record` with text fallback patterns from [`input.md`](./input.md).

## Out of scope

Expo components, Lottie packs, light theme, desktop visualizer port, changing macro bundles or FSM.
