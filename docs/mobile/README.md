# Mobile UX docs (v1 / prototype)

> **v1/prototype** · Part of epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Feeds [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Unblocks E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) · Macros: [#296](https://github.com/saman-mb/agentic-synth/issues/296) · Input: [#297](https://github.com/saman-mb/agentic-synth/issues/297) · IA closed via [#295](https://github.com/saman-mb/agentic-synth/issues/295).

Single-screen mobile information architecture for Tambra: one chrome surface, one bottom sheet, and a Say → Hear → Shape → Variations → Keep state machine. Zero app code in this tree — these docs are the ratified contract for E5 and sibling design stories.

**Bar:** iterative prototype; decent experience, not perfection. Captain / UX sign-off already recorded on [#295](https://github.com/saman-mb/agentic-synth/issues/295#issuecomment-5480709622).

## Reading order

1. [Information architecture](./ia.md) — screen inventory, sheet contents per state, Keep persistence, naming
2. [State machine](./state-machine.md) — `MobileState` transitions, entry/exit, interrupts
3. [Cut-list](./cut-list.md) — desktop features deferred from mobile v1, with revisit triggers
4. [Macros](./macros.md) — four knobs, bundles, prompt→amounts, thumb interaction (#296)
5. [Input](./input.md) — voice + text capture in `say`, degraded states (#297)

## Spec map

| Doc | Owns |
|---|---|
| [ia.md](./ia.md) | Chrome elements (`el.*`), per-state sheet bodies, Keep persistence |
| [state-machine.md](./state-machine.md) | States, legal edges, interrupts, error recovery |
| [cut-list.md](./cut-list.md) | What mobile deliberately omits vs desktop |
| [macros.md](./macros.md) | `el.macros` / `macro.0`…`3`, fixed bundles, overlap rule, plan amounts |
| [input.md](./input.md) | `input.voice` / `input.text`, tap-to-record, deny/offline/fail recovery |

## Future siblings

| Issue | Path | Hooks |
|---|---|---|
| [#296](https://github.com/saman-mb/agentic-synth/issues/296) | [macros.md](./macros.md) | **Landed** — `el.macros`, Shape macros |
| [#297](https://github.com/saman-mb/agentic-synth/issues/297) | [input.md](./input.md) | **Landed** — `say` capture substates + text fallback |
| [#298](https://github.com/saman-mb/agentic-synth/issues/298) | tokens / art | `el.visualizer` chrome only (stub until issue lands) |

Do not rename `MobileState` values or split the single sheet when filling remaining stubs.

## Out of scope here

App / Expo code, design tokens, pixel mockups, ADR amendments, desktop `apps/web` changes, engine DSP.
