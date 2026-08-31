# Mobile UX docs (v1 / prototype)

> **v1/prototype** · Part of epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Unblocks E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) · Art/tokens: [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Macros: [#296](https://github.com/saman-mb/agentic-synth/issues/296) · Input: [#297](https://github.com/saman-mb/agentic-synth/issues/297) · IA closed via [#295](https://github.com/saman-mb/agentic-synth/issues/295).

Single-screen mobile information architecture for Tambra: one chrome surface, one bottom sheet, and a Say → Hear → Shape → Variations → Keep state machine. Zero app code in this tree — these docs are the ratified contract for E5 and sibling design stories.

**Bar:** iterative prototype; decent experience, not perfection. Captain / UX (and art) proto sign-off recorded on the epic stories (#295–#298).

## Reading order

1. [Information architecture](./ia.md) — screen inventory, sheet contents per state, Keep persistence, naming
2. [State machine](./state-machine.md) — `MobileState` transitions, entry/exit, interrupts
3. [Cut-list](./cut-list.md) — desktop features deferred from mobile v1, with revisit triggers
4. [Macros](./macros.md) — four knobs, bundles, prompt→amounts, thumb interaction (#296)
5. [Input](./input.md) — voice + text capture in `say`, degraded states (#297)
6. [Art direction](./art-direction.md) — dark theme, knobs paint, visualizer + fps budget (#298)
7. [Tokens](./tokens.json) + [contrast companion](./tokens.md) — machine-readable color/type/space/radius/elevation (#298)

## Spec map

| Doc | Owns |
|---|---|
| [ia.md](./ia.md) | Chrome elements (`el.*`), per-state sheet bodies, Keep persistence |
| [state-machine.md](./state-machine.md) | States, legal edges, interrupts, error recovery |
| [cut-list.md](./cut-list.md) | What mobile deliberately omits vs desktop |
| [macros.md](./macros.md) | `el.macros` / `macro.0`…`3`, fixed bundles, overlap rule, plan amounts |
| [input.md](./input.md) | `input.voice` / `input.text`, tap-to-record, deny/offline/fail recovery |
| [art-direction.md](./art-direction.md) | Dark instrument look, knob paint, `el.visualizer` aesthetic + fps |
| [tokens.json](./tokens.json) / [tokens.md](./tokens.md) | Tokens + WCAG AA pairings for E5 |

## Story status

| Issue | Path | Hooks |
|---|---|---|
| [#295](https://github.com/saman-mb/agentic-synth/issues/295) | [ia.md](./ia.md), [state-machine.md](./state-machine.md), [cut-list.md](./cut-list.md) | **Landed** |
| [#296](https://github.com/saman-mb/agentic-synth/issues/296) | [macros.md](./macros.md) | **Landed** — `el.macros`, Shape macros |
| [#297](https://github.com/saman-mb/agentic-synth/issues/297) | [input.md](./input.md) | **Landed** — `say` capture substates + text fallback |
| [#298](https://github.com/saman-mb/agentic-synth/issues/298) | [art-direction.md](./art-direction.md), [tokens.json](./tokens.json), [tokens.md](./tokens.md) | **Landed** — dark tokens + viz/knob art |

Do not rename `MobileState` values or split the single sheet when extending these docs.

## Out of scope here

App / Expo code, pixel mockups, ADR amendments, desktop `apps/web` changes, engine DSP.
