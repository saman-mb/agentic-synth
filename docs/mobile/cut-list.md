# Mobile cut-list (v1)

> **v1/prototype** · Part of epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · Feeds [#296](https://github.com/saman-mb/agentic-synth/issues/296) / [#297](https://github.com/saman-mb/agentic-synth/issues/297) / [#298](https://github.com/saman-mb/agentic-synth/issues/298) · Unblocks E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) · Closes [#295](https://github.com/saman-mb/agentic-synth/issues/295) AC.

What the phone **deliberately omits** so the single screen + sheet in [ia.md](./ia.md) stays teachable. State machine: [state-machine.md](./state-machine.md). Index: [README](./README.md). Epic scope: [#290](https://github.com/saman-mb/agentic-synth/issues/290).

Captain / UX sign-off: [#295 comment](https://github.com/saman-mb/agentic-synth/issues/295#issuecomment-5480709622).

**What remains on mobile:** brand, visualizer, macros, play, Say CTA, sheet, weak library, status — see [ia.md §Screen inventory](./ia.md#screen-inventory-chrome).

Each row: stable `cut.*` id, desktop locus, rationale, revisit trigger.

| ID | Cut item | Desktop locus | Rationale | Revisit trigger |
|---|---|---|---|---|
| `cut.mod_matrix` | Mod matrix editor | Right-column / mod UI | Thumb reach + cognitive load; macros cover the morph verbs musicians need first | Users hit a wall shaping motion that macros cannot express; or #296 proves a one-axis mod assign is required |
| `cut.constellation` | Constellation / 3D mod view | Mod matrix alt tab | Dense 3D viz fights small screens and battery; same data as matrix | Desktop constellation becomes the primary teach path and a 2D mobile cousin is demanded |
| `cut.modules_hood` | Full ModulesGrid / “Open the hood” | Center module columns | Desktop-era param sprawl; contradicts one-sheet IA | Power users need OSC/filter/env surgery on device after macros ship |
| `cut.dictionary` | Dictionary editor | Tools / vocabulary UI | Editing agent lexicon is rare vs saying a sound; wrong primary job | Support / power users need on-device vocab fixes without desktop |
| `cut.telemetry` | Telemetry dashboard / morph JSONL UI | Tools / telemetry | Debugging surface, not musician surface; privacy and chrome noise | Internal dogfood or morph research needs an optional debug build flag |
| `cut.ab` | A/B compare | TopBar A/B | Twin snapshots compete with Variations; one exploration verb on phone | Musicians demand instant A↔B after Keep without re-entering Variations |
| `cut.undo_bar` | Undo/redo chrome | TopBar undo/redo | Sheet + regenerate cover most mistakes; permanent undo chrome steals vertical space | Irreversible macro edits become common and regenerate is too slow |
| `cut.multicol` | Multi-column desktop layout | Phase-4 grid | Phone is one column by physics | Tablet / foldable layout pass under a later epic |
| `cut.midi_learn` | Per-knob MIDI learn | Knob learn affordances | Mobile session is usually touch-first; learn maps are desktop/hardware ritual | Hardware controller users bring phones into the same set as the plugin |
| `cut.midi_keyboard` | On-screen MIDI keyboard | Bottom keyboard | Competes with sheet + Say CTA; `el.play` / gate covers audition | Melodic sketching on phone becomes a core job (not just timbre) |
| `cut.bounce` | Bounce to WAV | Export / bounce | Sharing a preset blob beats encoding audio in v1; storage and permissions cost | Social / DAW-less share of a rendered clip becomes a top request |
| `cut.history_drawer` | Tools drawer history / agent log browser | Tools drawer | Chat archaeology ≠ save-the-sound; Keep stores prompt + patch | Users need to recover an earlier agent turn without re-prompting |
| `cut.rationale` | “Why this patch?” panel | Agent rationale UI | Interesting, not essential to hear → shape → keep | Trust / pedagogy experiments show rationale lifts Keep rate |
| `cut.meters` | CPU / MIDI / OUT meter cluster | TopBar meters | Engineer chrome; `el.status` covers calm failures | Field reports of silent audio that status cannot explain |
| `cut.topbar_chrome` | Desktop TopBar compare / preset chrome | TopBar | Replaced by `el.brand`, `el.library`, sheet actions | Tablet landscape wants a denser header without multi-column |
| `cut.settings_deep` | Deep settings (theme lab, MIDI device enum) | Settings | Keep only audio-route essentials later; theme lab is #298 territory | Shipping builds need device picker / theme beyond a single audio-route screen |

Epic-ratified anchors (must stay cut unless revisit fires): mod matrix / constellation, dictionary, telemetry, A/B, undo bar, multi-column.

## Non-goals for this file

Do not invent replacement mobile screens for cut items here. When a revisit trigger fires, open a new issue under epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) (or a successor) and amend this table — do not silently grow chrome.

## See also

- [Information architecture](./ia.md) — what stayed
- [State machine](./state-machine.md)
- [Mobile docs index](./README.md)
