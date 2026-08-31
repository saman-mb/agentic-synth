# Mobile voice + text input

> **v1/proto** · epic [#290](https://github.com/saman-mb/agentic-synth/issues/290) · feeds E5 [#294](https://github.com/saman-mb/agentic-synth/issues/294) / voice [#276](https://github.com/saman-mb/agentic-synth/issues/276) · Part of [#297](https://github.com/saman-mb/agentic-synth/issues/297)

How a musician describes a sound on mobile: voice capture and text fallback inside `MobileState` **`say`**, with every degraded path named. Zero app / Expo code here — pattern contract only.

**Bar:** iterative prototype; decent experience, not perfection. Captain / UX proto bar: [#295 comment](https://github.com/saman-mb/agentic-synth/issues/295#issuecomment-5480709622).

Index: [README](./README.md) · IA hooks: [ia.md](./ia.md) (`el.input_cta`, `sheet.say`, `input.voice` \| `input.text`) · FSM: [state-machine.md](./state-machine.md).

## Surface hooks (from IA / FSM)

| Hook | Role |
|---|---|
| `el.input_cta` | Chrome Say affordance — opens / focuses capture in `say` |
| `sheet.say` | Expanded sheet: transcript / text field, Cancel, Send, mic in-flight UI |
| `input.voice` | Active capture / STT path |
| `input.text` | Typed (or edited transcript) path |
| `el.status` | Thin calm status line for soft failures (permission, network, STT) |
| `sheet.error` | Hard failures only (subsystem crash) — see mapping below |

Capture **never** invents a second sheet or a parallel FSM. Capture substates live **inside** `say` (or soft-fail into `input.text` while still `say`).

## Capture gesture (v1 choice)

**v1: tap-to-record** (`input.gesture = tap_to_record`).

| Step | Behavior |
|---|---|
| Tap mic / record | Enter `recording`; start capture |
| Tap stop (or “Done”) | End capture → `transcribing` |
| Auto max length | Soft cap (~30–45s); stop → `transcribing` with calm status “Stopped at max length” |
| Cancel | Abort capture / STT; keep any prior draft text; stay `say` |

### Rejected for v1: push-to-talk (PTT)

| Why rejected (mobile v1) | Notes |
|---|---|
| Hold conflicts with sheet grabber / scroll / one-thumb Shape later | Accidental release truncates prompts |
| Call / focus-loss already aborts capture ([state-machine](./state-machine.md)) | PTT “hold forever” fights interrupt matrix |
| Desktop `PushToTalk` remains a desktop concern | Mobile does not inherit desktop hold UX for proto |

Revisit PTT only if usability tests show tap-to-record false starts dominate **and** hold can be isolated from the sheet grabber.

## Capture substates (`input.*`)

Normative UI + copy **direction** (not final strings). Map to `MobileState` / sheet:

| Substate | `MobileState` | Mode | UI / copy direction |
|---|---|---|---|
| `idle` | `say` (or entering via `el.input_cta`) | `input.voice` ready **or** `input.text` | Mic + text field visible; hint “Describe a sound” / “Tap to record”; Send disabled until non-empty usable prompt |
| `recording` | `say` | `input.voice` | Live waveform / timer; primary **Stop**; secondary **Cancel**; duck prior preview ([FSM `say`](./state-machine.md)) |
| `transcribing` | `say` | `input.voice` | Progress / “Getting that…”; **Cancel** aborts STT, returns to idle with empty or partial draft; do not auto-Send |
| `success` | `say` | usually → `input.text` | Transcript lands in editable field; user may edit then **Send** → `hear`; mic available to re-record |
| `denied` | **`say`** (stay) | force `input.text` | Mic muted/disabled; text field focused; one-tap **Open settings** (OS permission); calm status — **not** `sheet.error` for ordinary deny |
| `failed` | `say` (soft) or `error` (hard) | recover in sheet | Soft STT/network fail: stay `say`, show cause + Retry / Type instead; hard input-subsystem crash: → `error` with `returnState=say` |

**Hard rule:** mic permission denied is a **soft** degrade into text — never a dead end, never a silent no-op. Ordinary deny does **not** exit `say` for `error`.

### Mapping summary

```
input.idle | recording | transcribing | success | denied | failed(soft)
    └── all under MobileState.say  (+ sheet.say / el.status)

input.failed(hard) / input subsystem crash
    └── MobileState.error  (returnState=say)
```

## Mic permission denied → text fallback (feeds #276)

When OS reports mic denied / restricted:

1. Stay in `MobileState` **`say`**.
2. Switch mode to **`input.text`**; focus the text field.
3. Disable / grey the mic control; show why in one calm line (`el.status` or inline under mic): e.g. “Microphone off — type instead.”
4. Offer **one primary deep-link** to the OS app permission settings (platform Settings URL / intent). Label direction: “Open settings”.
5. After returning from Settings, re-query permission; if granted, re-enable mic without wiping draft text.
6. Product surface for a consistent failure kind (`mic_denied`) may align with desktop [#276](https://github.com/saman-mb/agentic-synth/issues/276) — mobile still prefers **inline text fallback** over a blocking banner-only path.

Never require the user to kill the app to recover.

## Network / offline

Explicit v1 behavior (no silent dead-ends):

| Capability | Offline / no network | Online |
|---|---|---|
| Open `say`, type prompt | **Works** | Works |
| Edit draft / Cancel | **Works** | Works |
| Tap-to-record → local buffer | **Works** (device mic) | Works |
| Cloud / remote STT | **Fails soft** → `failed` in `say` | `transcribing` → `success` |
| On-device STT (if shipped later) | May work; same substates | Same |
| **Send** / generate (`say` → `hear`) | **Blocked** with explicit status: “You’re offline — connect to generate.” Stay `say`; keep draft | Proceeds to `hear` |
| Browse Keep / Shape prior patch | Unchanged by this doc (session scratch / local Keep) | — |

If connectivity drops mid-`transcribing`: abort STT → soft `failed`; preserve audio buffer only if cheap — otherwise prompt **Re-record** or **Type instead**. Never auto-Send a partial/empty transcript.

## Unusable transcript recovery

Treat as soft `failed` or `success` with empty/garbled text — always recoverable inside `say`:

| Case | Detection (v1) | Recovery |
|---|---|---|
| Empty after STT | Trimmed length 0 | Status “Didn’t catch that”; actions: **Re-record**, focus text with hint |
| Near-empty / noise | Below min chars / confidence floor (impl detail) | Same as empty; optional hint examples (“warm pad”, “detuned bells”) |
| Garbled / wrong language | User judges | Editable field always; **Re-record** replaces; **Send** only when user accepts text |
| User rejects transcript | Explicit clear / re-record | Clear field or overwrite on new `success` |

Hints are **examples**, not a dictionary editor ([cut-list](./cut-list.md)). No auto-generate on unusable input.

## Cancel gesture

| Context | Gesture | Result |
|---|---|---|
| `recording` | Cancel / close capture | Abort audio; stay `say`; draft unchanged if any prior text |
| `transcribing` | Cancel | Abort STT; stay `say` idle; no auto-Send |
| Sheet Cancel / clear (IA) | Clear intent | → `idle` when no pending patch intent ([FSM](./state-machine.md)); else stay `say` with empty field |
| Call / background | Interrupt | Abort capture; keep draft; freeze in `say` ([interrupt matrix](./state-machine.md)) |

Cancel **never** jumps to `hear`.

## In-flight UI / copy direction

| Phase | Primary | Secondary | Status tone |
|---|---|---|---|
| Recording | Stop | Cancel | Neutral timer; optional “Listening…” |
| Transcribing | — (or Cancel only) | Cancel | “Getting that…” — no fake progress % required for proto |
| Soft fail | Re-record / Try again | Type instead | One cause line; no stack traces |
| Denied | Open settings | Type here (already focused) | “Microphone off” |
| Offline Send | — | Dismiss status | “You’re offline — connect to generate.” |
| Success | Send | Re-record / Edit | Transcript in field; Send enabled when non-empty |

Copy stays short and calm — matches `el.status` / `sheet.error` tone in [ia.md](./ia.md).

## Send gate

**Send** (→ `hear`) requires a **user-accepted non-empty prompt** in the text field (typed or edited transcript). Voice `success` alone does not auto-Send in v1 — musician confirms. Empty / whitespace never leaves `say`.

## Rejected alternatives (summary)

| Alternative | Why not v1 |
|---|---|
| Push-to-talk as default | Hold vs sheet/thumb conflicts; see above |
| Voice-only (no text) | Breaks deny / quiet environments / accessibility |
| Auto-Send on STT success | Unusable transcripts would burn agent calls; user must confirm |
| Hard `error` on mic deny | Violates graceful degradation; text fallback is the path |
| Silent offline Send | Dead-end; must surface “You’re offline…” |

## Out of scope

App / Expo widgets, real STT vendor choice, pixel mockups, WCAG audit matrices, desktop PushToTalk refactors beyond the #276 feed note, art tokens (#298), changing `MobileState` names.
