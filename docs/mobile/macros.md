# Mobile macro knobs

> **v1/proto** · epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · feeds E3 [#292](https://github.com/saman-mb/agentic-synth/issues/292) · Part of [#296](https://github.com/saman-mb/agentic-synth/issues/296)

Four large macro knobs on chrome (`el.macros`) so a musician can shape the working patch with one thumb in `shape`. This doc is the **contract** for E2/E3 — labels, fixed parameter bundles, overlap rules, prompt→macro starting values, and knob interaction. Zero app / DSP code here.

**Bar:** iterative prototype; decent mobile UX, not perfection. Matches desktop’s four macros (`macro1`…`macro4`), not six.

Index: [README](./README.md) · IA: [ia.md](./ia.md) · FSM: [state-machine.md](./state-machine.md).

## Alignment with desktop

Desktop already exposes exactly four macro sources:

| Desktop (`ModSourceId`) | Mobile index | Notes |
|---|---|---|
| `macro1` | `macro.0` | See `MOD_SOURCES` / `macroIndexOf` in [`libs/data/src/lib/modulation.ts`](../../libs/data/src/lib/modulation.ts) |
| `macro2` | `macro.1` | |
| `macro3` | `macro.2` | |
| `macro4` | `macro.3` | |

Desktop UI also renders four knobs (`MacroBar`, play-surface hero row). Mobile **fixes** musician labels and **fixed** param bundles for v1 (desktop still allows freeform matrix routes). Param IDs are the dotted paths registered in [`src/agent/ParamMap.cpp`](../../src/agent/ParamMap.cpp). Numeric ranges match [`libs/data/src/lib/paramRanges.ts`](../../libs/data/src/lib/paramRanges.ts) (`PARAM_RANGES`).

## Count and inventory

Exactly **4** macros (`macro.0`…`macro.3`). Slugs are stable `MacroId`s for Keep / plan JSON; UI shows the user label.

| Index | MacroId | User label | One-line (musician) |
|---|---|---|---|
| `macro.0` | `brightness` | Brightness | Open or darken the tone |
| `macro.1` | `movement` | Movement | Add or calm motion / wobble |
| `macro.2` | `space` | Space | Push the sound into a room or keep it dry |
| `macro.3` | `body` | Body | Soften the hit or thicken the edge |

## Bundles (disjoint)

**v1 rule: bundles are pairwise disjoint** — no shared param IDs across macros. Overlap conflict resolution is therefore a no-op for proto. If a later revision introduces shared params, use the deterministic fallback: **higher-index macro wins** on that param (apply `macro.0` then `macro.1` … `macro.3`; last write sticks). Prefer keeping bundles disjoint instead.

Knob position is always **normalized `0…1`**. Projection onto engine params:

```
effective = base + f(position) * span   // then clamp to PARAM_RANGES
```

where `f` encodes direction/curve. `base` is the patch value after generate (before macro offset). E3 owns the exact projector; this table is the contract.

### `macro.0` — Brightness (`brightness`)

- **Description:** Brighter opens the filter; darker closes it. A little resonance moves opposite so the dark end stays musical, not hollow.
- **Default position:** `0.55` (slightly open; agent may override via plan — see prompt contract).

| Param ID | Range | Direction / curve | Weight |
|---|---|---|---|
| `filter.cutoff_hz` | 20…20000 Hz | **Up** with position (prefer log/perceptual mapping across Hz) | Primary |
| `filter.resonance` | 0…1 | **Mild opposite** — as cutoff opens, resonance eases slightly | Mild |

### `macro.1` — Movement (`movement`)

- **Description:** More movement deepens LFO 1; a little rate follows so motion feels alive, not just wider.
- **Default position:** `0.35`

| Param ID | Range | Direction / curve | Weight |
|---|---|---|---|
| `lfo.0.depth` | 0…1 | **Up** with position (linear) | Primary |
| `lfo.0.rate_hz` | 0.01…20 Hz | **Mild up** (prefer log across Hz) | Mild |

### `macro.2` — Space (`space`)

- **Description:** Wet vs dry room. Mix is the main feel; size blooms with it so big spaces don’t stay tiny.
- **Default position:** `0.30`

| Param ID | Range | Direction / curve | Weight |
|---|---|---|---|
| `reverb.mix` | 0…1 | **Up** with position (linear) | Primary |
| `reverb.size` | 0…1 | **Mild up** (linear) | Mild |

### `macro.3` — Body (`body`)

- **Description:** Higher body softens the initial hit (slower attack) and adds a touch of filter drive for weight. Keep the bundle small.
- **Default position:** `0.40`

| Param ID | Range | Direction / curve | Weight |
|---|---|---|---|
| `amp_env.attack_s` | 0…10 s | **Inverse-ish** — higher macro → longer attack (prefer soft curve / capped span so max isn’t a 10s pad unless intended) | Primary |
| `filter.drive` | 0…1 | **Mild up** (linear) | Mild |

**Not in v1 Body:** `osc.*.volume` — left out so volume stays under patch / master, bundles stay disjoint, and thumb control stays predictable.

## Overlap / conflict

| Case | Resolution |
|---|---|
| v1 fixed bundles | **Disjoint** — no shared params; no runtime arbitration |
| Future shared param | Apply macros in index order `0→3`; **higher index wins** on that param |
| Macro vs raw hood edit | Mobile v1 has no hood (`cut.modules_hood`); N/A until revisit |

## Prompt → macro starting values

Same shape as desktop `plan.macros` after patch generate.

1. **Producer:** agent / plan layer (E2 orchestration + planner output), not the UI.
2. **Emit:** after a successful generate, the plan includes macro amounts in **`0…1`** for up to four slots (aligned with desktop `plan.macros.slice(0, 4)` in `apps/web`).
3. **Consumers:** E2 (wire into mobile session / Keep snapshot) and E3 (apply bundles when projecting to engine params).
4. **Fields (contract):**
   - Slot index `0…3` ↔ `macro.{i}` ↔ desktop `macro{i+1}`
   - `amount` / value: `number` in `[0, 1]`
   - Optional `name` / `label`: if present and non-empty, may override the default user label for that session (desktop already does this); v1 mobile may ignore rename and keep Brightness / Movement / Space / Body for chrome consistency — product choice for E5, but amounts always apply.
5. **Missing slot:** use the **default position** from the inventory tables above.
6. **Out-of-range amount:** clamp to `[0, 1]` before UI / projection.
7. **Example intent:** prompt “warm analog pad” → plan might set Brightness low-mid, Space elevated, Movement low, Body mid-high; exact numbers are planner policy, not fixed in this doc — only the **who emits / who consumes / 0…1** contract is normative.

## Knob interaction (UX)

Signed off for [#296](https://github.com/saman-mb/agentic-synth/issues/296) AC at the **spec** level (prototype bar). No art tokens here (#298 owns chrome polish).

| Concern | Spec |
|---|---|
| Hit target | Min ~**48–56 dp** touch target per knob |
| Reach | One-thumb usable in `shape` while sheet is peek/collapsed; knobs live on `el.macros` chrome, not buried in the sheet |
| Primary gesture | **Vertical drag** to change `0…1` value |
| Fine adjust | Slower drag sensitivity **or** long-press → precision mode (smaller delta per pixel) |
| Discrete | No mandatory detent; continuous `0…1` |
| Feedback | Value change must be audible via working patch while `el.play` / gate is active; visual feedback is E5 + #298 |

## Rejected alternatives

| Alternative | Why rejected (v1/proto) |
|---|---|
| **6 macros** | Desktop + `modulation.ts` are four (`macro1`…`macro4`); six would fork plan/Keep/mod sources and crowd one-thumb chrome. Revisit only if Captain expands desktop count. |
| **Per-param knobs on mobile** | Defeats the “shape with thumb” goal; hood/modules already on cut-list. |
| **Freeform assignable macros in v1** | Desktop matrix is powerful but heavy; mobile needs fixed musician labels and deterministic bundles for E3. |
| **Summing overlapping macros (desktop-style)** | Desktop sums mod amounts; mobile v1 prefers **disjoint bundles** for predictable thumb feel. Document higher-index-wins only as future escape hatch. |
| **Osc volume in Body** | Couples loudness to “feel”; risks surprise gain and future overlap with any level macro. |
| **Using `filter.env_mod` for Brightness mild** | Valid alternate; resonance-opposite chosen for simpler “open/dark” story without bipolar env-mod explanation. |

## Out of scope

App / Expo widgets, DSP projector implementation (E3 #292), design tokens / art (#298), freeform mod matrix on mobile, changing desktop `macro1`…`macro4` count.
