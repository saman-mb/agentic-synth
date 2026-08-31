# Mobile information architecture

> **v1/prototype** · Part of epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Macros: [macros.md](./macros.md) ([#296](https://github.com/saman-mb/agentic-synth/issues/296)) · Feeds [#297](https://github.com/saman-mb/agentic-synth/issues/297) / [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Unblocks E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) · Closes [#295](https://github.com/saman-mb/agentic-synth/issues/295) AC.

Single screen + one bottom sheet. An engineer who has never seen the desktop app should be able to name every control from this doc alone. State transitions live in [state-machine.md](./state-machine.md). Desktop deferrals live in [cut-list.md](./cut-list.md). Index: [README](./README.md).

**Bar:** iterative prototype; decent experience, not perfection. Captain / UX sign-off already recorded on [#295](https://github.com/saman-mb/agentic-synth/issues/295#issuecomment-5480709622).

## Surface rule

Exactly **one** chrome surface and **one** bottom sheet (`el.sheet`). No multi-route nav, no “open the hood,” no desktop wrap. The sheet is presentation of `MobileState`, not a second state machine.

## Screen inventory (chrome)

Always mounted. Visibility may dim; do not unmount for state changes.

| Element ID | What it is | Notes |
|---|---|---|
| `el.brand` | Wordmark / minimal brand mark | Hero-level brand signal; not just a nav chip |
| `el.visualizer` | Full-width visualizer plane | Aesthetic + motion owned by [#298](https://github.com/saman-mb/agentic-synth/issues/298); content shows the sound |
| `el.macros` | 4 macro controls (`macro.0`…`macro.3`) | Fixed v1 set in [macros.md](./macros.md) (#296): Brightness / Movement / Space / Body |
| `el.play` | Play / hold / gate for audition | Primary hear affordance when sheet is peek/collapsed |
| `el.input_cta` | Primary Say affordance | Mic + text entry point; capture patterns = [#297](https://github.com/saman-mb/agentic-synth/issues/297) (`input.md`) |
| `el.sheet` | Bottom sheet + grabber | One sheet for all states; body swaps by `MobileState` |
| `el.library` | Weak entry to kept presets | List / peek only — not the desktop browser |
| `el.status` | Thin status / failure line | Calm copy; no telemetry chrome |

**Not on chrome (see [cut-list](./cut-list.md)):** mod matrix, constellation, modules grid, A/B, undo bar, meters cluster, dictionary, hood, on-screen MIDI keyboard.

## Sheet contents by state

| State | Sheet id | Default | Body (engineer checklist) |
|---|---|---|---|
| `idle` | `sheet.idle` | Collapsed or peek | Short prompt hint (“Describe a sound”); optional recent kept row; start Say via `el.input_cta` |
| `say` | `sheet.say` | Expanded | Transcript / text field; Cancel; Send / generate; mic in-flight affordances (detail → [#297](https://github.com/saman-mb/agentic-synth/issues/297)) |
| `hear` | `sheet.hear` | Peek or expanded | Progress / “Building your sound…”; Cancel generate; first-listen / skip-to-shape when patch lands |
| `shape` | `sheet.shape` | Peek | Macro label echo + optional one-line prompt echo; actions: **Variations**, **Keep**; regenerate / new idea |
| `variations` | `sheet.variations` | Expanded | Variant list or carousel; Select; More (→ `hear`); Back to Shape; optional Keep on selection |
| `keep` | `sheet.keep` | Expanded | Name field (default from prompt fragment); Confirm Keep; Cancel |
| `error` | `sheet.error` | Expanded | User-facing cause; Retry; Edit (back toward `say` / `shape`); Dismiss |

### `idle` — `sheet.idle`

- Hint copy for first-run and returning users
- Optional “Recent” kept presets (taps load into Shape path — exact edge deferred to E5; v1 may open library only)
- No generate progress, no Keep form

### `say` — `sheet.say`

- Editable text / live transcript
- Cancel / clear
- Send
- Voice vs text mode hooks (`input.voice` \| `input.text`) — behavior owned by [#297](https://github.com/saman-mb/agentic-synth/issues/297)

### `hear` — `sheet.hear`

- In-flight progress
- Cancel / edit prompt
- When patch lands: first-listen controls or auto-advance affordance into Shape

### `shape` — `sheet.shape`

- Working-patch context (prompt echo)
- Pointers to `el.macros` / `el.play` (chrome owns the knobs)
- Primary actions: Variations, Keep
- Escape hatches: new idea (`say`), regenerate (`hear`)

### `variations` — `sheet.variations`

- Carousel / list of variants (identity only until Keep)
- Select → becomes current working patch (`shape`)
- More → another generate (`hear`)
- Keep optional if a variant is selected

### `keep` — `sheet.keep`

- Name field
- Confirm / Cancel
- On Confirm: write Keep library row (below), then → `idle`

### `error` — `sheet.error`

- Short cause
- Retry / Edit / Dismiss
- Remembers `returnState` (see [state-machine](./state-machine.md))

## Keep persistence contract

**Event:** user taps Confirm in `keep`.

### Saved (v1)

| Field | Required | Notes |
|---|---|---|
| `preset.id` | yes | Stable local id |
| `preset.name` | yes | User-editable; default from prompt fragment |
| `preset.prompt` | yes | Source utterance / text that produced the kept sound |
| `preset.patch` | yes | Full working patch snapshot (same `PatchParams` / `PatchStruct` POD as desktop — no mobile-only schema) |
| `preset.macros` | yes | Index-ordered `[number × 4]` knob positions in `0…1` (`macro.0`…`macro.3`; see [macros.md](./macros.md)) |
| `preset.variation` | yes | **Chosen variation identity only:** `{ index, seed? }`. Rejected siblings are **not** stored |
| `preset.createdAt` | yes | ISO-8601 timestamp |

### Explicitly not saved (v1)

Full chat / agent rationale · rejected variation patches · telemetry · A/B twin · MIDI maps · dictionary · bounce WAV

### Session scratch (not the Keep library)

Survives background; cleared after successful Keep or explicit discard:

- Draft prompt / name field text
- Frozen `MobileState`
- Current working patch id (if any)
- In-flight request id
- Variation list + selection index (when in `variations`)
- `returnState` when in `error`

## Naming conventions

| Kind | Convention | Example |
|---|---|---|
| States | `MobileState` snake | `shape`, `variations` |
| Elements | `el.*` | `el.macros` |
| Macros | `macro.{index}` + `MacroId` slug ([macros.md](./macros.md)) | `macro.0` / `brightness` |
| Sheet slots | `sheet.{state}` | `sheet.keep` |
| Input modes | `input.voice` \| `input.text` (#297) | — |
| Cuts | `cut.*` | `cut.ab` |
| Doc links | Relative under `docs/mobile/` | `[State machine](./state-machine.md)` |
| Tokens (#298) | CSS-var style keys in JSON | `color.bg.0` |

**Rule for later stories:** [#297](https://github.com/saman-mb/agentic-synth/issues/297)–[#298](https://github.com/saman-mb/agentic-synth/issues/298) may **add** files and fill reserved hooks. They must not rename `MobileState` values or split the single sheet. [#296](https://github.com/saman-mb/agentic-synth/issues/296) macros contract is in [macros.md](./macros.md).

## Spec siblings

- [macros.md](./macros.md) — #296 (landed)

## Future stubs (do not create yet)

- [input.md](./input.md) — #297
- Visual tokens / art — #298 (`el.visualizer` only)

## v1 non-goals

Pixel mockups · WCAG matrices · push-to-talk debate detail · Expo scaffold · offline-first agent · cloud sync · parallel audio/UI state machines · new patch schema (macro bundles/curves → [macros.md](./macros.md))

## See also

- [State machine](./state-machine.md)
- [Cut-list](./cut-list.md)
- [Mobile docs index](./README.md)
