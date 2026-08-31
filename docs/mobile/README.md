# Mobile UX docs (v1 / prototype)

> **v1/prototype** · Part of epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Feeds [#296](https://github.com/saman-mb/agentic-synth/issues/296) / [#297](https://github.com/saman-mb/agentic-synth/issues/297) / [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Unblocks E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) · Closes [#295](https://github.com/saman-mb/agentic-synth/issues/295) AC.

Single-screen mobile information architecture for Tambra: one chrome surface, one bottom sheet, and a Say → Hear → Shape → Variations → Keep state machine. Zero app code in this tree — these docs are the ratified contract for E5 and sibling design stories.

**Bar:** iterative prototype; decent experience, not perfection. Captain / UX sign-off already recorded on [#295](https://github.com/saman-mb/agentic-synth/issues/295#issuecomment-5480709622).

## Reading order

1. [Information architecture](./ia.md) — screen inventory, sheet contents per state, Keep persistence, naming
2. [State machine](./state-machine.md) — `MobileState` transitions, entry/exit, interrupts
3. [Cut-list](./cut-list.md) — desktop features deferred from mobile v1, with revisit triggers

## Spec map

| Doc | Owns |
|---|---|
| [ia.md](./ia.md) | Chrome elements (`el.*`), per-state sheet bodies, Keep persistence |
| [state-machine.md](./state-machine.md) | States, legal edges, interrupts, error recovery |
| [cut-list.md](./cut-list.md) | What mobile deliberately omits vs desktop |

## Future siblings (not in #295)

| Issue | Planned path | Hooks |
|---|---|---|
| [#296](https://github.com/saman-mb/agentic-synth/issues/296) | [macros.md](./macros.md) | `el.macros`, Shape macros |
| [#297](https://github.com/saman-mb/agentic-synth/issues/297) | [input.md](./input.md) | `say` / `error` input modes |
| [#298](https://github.com/saman-mb/agentic-synth/issues/298) | tokens / art | `el.visualizer` chrome only |

Those files are stub links until those issues land. Do not rename `MobileState` values or split the single sheet when filling them in.

## Out of scope here

App / Expo code, design tokens, pixel mockups, ADR amendments, desktop `apps/web` changes, engine DSP.
