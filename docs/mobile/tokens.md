# Mobile design tokens — contrast & consumption

> **v1/proto** · epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Part of [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Feeds E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294)

Machine-readable tokens live in [`tokens.json`](./tokens.json). This companion documents **WCAG AA pairings** for the dark mobile palette and how E5 should consume keys. Art intent: [`art-direction.md`](./art-direction.md). Knob interaction contract (unchanged): [`macros.md`](./macros.md).

**Bar:** iterative prototype; decent mobile instrument experience, not perfection. Captain proto authorization clears art/look-and-feel gate for v1 (refine later).

## Palette intent

Tambra mobile is a **dark instrument surface** — charcoal void, warm copper accent (`accent.primary` `#D4A574`), cool visualizer teal (`accent.viz`), green play / coral record. Avoid purple-on-white “AI product” defaults; violet from older desktop REBRAND notes is **not** the mobile v1 signature.

## WCAG AA targets

| Role | Minimum contrast |
|---|---|
| Text (primary / secondary / tertiary / on-accent) | **≥ 4.5:1** |
| UI / controls / icons / focus borders | **≥ 3:1** |

Ratios below use relative luminance (sRGB) as in WCAG 2.x. Values rounded to two decimals.

### Documented pairings

| Pairing | Colors | Ratio | Target | Status |
|---|---|---|---|---|
| `text.primary / bg.canvas` | `#E8EBF0` on `#0E1016` | 15.91:1 | ≥4.5:1 (text) | pass |
| `text.primary / bg.panel` | `#E8EBF0` on `#14161D` | 15.12:1 | ≥4.5:1 (text) | pass |
| `text.primary / bg.raised` | `#E8EBF0` on `#1C1F28` | 13.77:1 | ≥4.5:1 (text) | pass |
| `text.secondary / bg.canvas` | `#9AA3B2` on `#0E1016` | 7.48:1 | ≥4.5:1 (text) | pass |
| `text.secondary / bg.panel` | `#9AA3B2` on `#14161D` | 7.10:1 | ≥4.5:1 (text) | pass |
| `text.tertiary / bg.canvas` | `#8B93A2` on `#0E1016` | 6.15:1 | ≥4.5:1 (text) | pass |
| `text.tertiary / bg.panel` | `#8B93A2` on `#14161D` | 5.84:1 | ≥4.5:1 (text) | pass |
| `text.disabled / bg.canvas` | `#7A8496` on `#0E1016` | 5.04:1 | ≥4.5:1 (text) | pass |
| `text.onAccent / accent.primary` | `#0A0B0F` on `#D4A574` | 8.84:1 | ≥4.5:1 (text) | pass |
| `text.onAccent / accent.play` | `#0A0B0F` on `#3DDC97` | 11.13:1 | ≥4.5:1 (text) | pass |
| `accent.primary / bg.canvas` | `#D4A574` on `#0E1016` | 8.54:1 | ≥3.0:1 (ui) | pass |
| `accent.primary / bg.panel` | `#D4A574` on `#14161D` | 8.12:1 | ≥3.0:1 (ui) | pass |
| `accent.play / bg.canvas` | `#3DDC97` on `#0E1016` | 10.76:1 | ≥3.0:1 (ui) | pass |
| `accent.record / bg.canvas` | `#E85D4C` on `#0E1016` | 5.52:1 | ≥3.0:1 (ui) | pass |
| `accent.viz / bg.canvas` | `#5B9FD4` on `#0E1016` | 6.66:1 | ≥3.0:1 (ui) | pass |
| `control.fill / control.track` | `#D4A574` on `#2A2E3A` | 6.08:1 | ≥3.0:1 (ui) | pass |
| `control.indicator / bg.knobPlate` | `#F0E6D8` on `#1A1D26` | 13.64:1 | ≥3.0:1 (ui) | pass |
| `border.focus / bg.panel` | `#D4A574` on `#14161D` | 8.12:1 | ≥3.0:1 (ui) | pass |
| `border.strong / bg.panel` | `#5A6378` on `#14161D` | 3.00:1 | ≥3.0:1 (ui) | pass |
| `icon.primary / bg.canvas` | `#E8EBF0` on `#0E1016` | 15.91:1 | ≥3.0:1 (ui) | pass |

**Do not** place `text.tertiary` on `accent.*` fills. Prefer `text.onAccent` for CTAs.

## Token groups (JSON)

| Group | Purpose |
|---|---|
| `color.*` | Dark surfaces, text, accents, borders, controls |
| `type.*` | Families + scale (sp) |
| `space.*` | Spacing scale + chrome/macro gaps (dp) |
| `radius.*` | Corner radii; knobs use full pill |
| `elevation.*` | Soft shadow lifts (no neon glow stacks) |
| `motion.*` | Durations for sheet/chrome (viz budget is fps, not duration) |
| `component.knob` | Touch **48–56 dp**, visual ≤ target; matches [`macros.md`](./macros.md) |
| `component.visualizer` | Full-width `el.visualizer`; **target 30 fps**, floor 24 |

## Consumption notes (E5)

1. Map JSON paths to platform tokens (e.g. `color.bg.canvas` → `--color-bg-canvas`).
2. Knob hit area ≥ `component.knob.touchTargetMinDp` (48); prefer 56. Visual diameter may be smaller inside the hit rect.
3. Do not invent a sixth macro or change vertical-drag semantics — interaction stays in [`macros.md`](./macros.md).
4. Visualizer must respect `targetFps` / `budgetFpsFloor` under audition load.

## Out of scope

Expo theme wiring, screenshot mocks, light theme, desktop CSS port.
