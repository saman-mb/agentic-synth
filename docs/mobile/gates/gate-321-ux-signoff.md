# Gate #321 — UX + Art Polish Sign-Off

**Epic:** [#294](https://github.com/saman-mb/agentic-synth/issues/294) Expo Mobile App Build (v1)  
**Branch:** `feat/epic-294-expo-mobile-app-build`  
**Date:** 2026-09-01  
**Verdict:** **PASS WITH CONDITIONS**

## Scope reviewed

Static + remediation review of `apps/mobile/` against E1 specs:

- `docs/mobile/ia.md` — chrome, sheet slots, Keep contract
- `docs/mobile/art-direction.md` — palette, visualizer plane, touch targets
- `docs/mobile/tokens.json` — color, type, space
- `docs/mobile/macros.md` — 4 macro bundles, 48–56 dp knobs
- `docs/mobile/input.md` — tap-to-record, offline generate block
- `docs/mobile/state-machine.md` — Say→Hear→Shape→Variations→Keep

## Remediation applied (gate closure)

| Finding | Resolution |
|---------|------------|
| P0 Variations prop mismatch | Fixed `BottomSheet` → `VariationsSheet` wiring |
| Boot trapped in `hear` | Demo boot lands in `shape` with scratch + active macros |
| Error sheet dead-end | `ErrorSheet` with Retry + Dismiss |
| Hear cancel missing | `HearSheet` Cancel during generate |
| Reduced-motion | Visualizer respects `AccessibilityInfo.isReduceMotionEnabled` |
| Touch targets <48dp | Sheet + Say CTA `minHeight: 48dp` |
| Focus indicators | Focus border using `border.focus` on primary Pressables |
| `el.library` missing | `LibraryPeek` on brand header |
| Shape prompt echo | Prompt fragment shown in Shape sheet |

## Passed criteria

- Token layer matches `docs/mobile/tokens.json` (dark instrument palette, copper accent)
- 4 macros: Brightness / Movement / Space / Body — vertical drag, 56dp hit targets
- Full loop implemented: Say → Hear → Shape → Variations → Keep
- Say input: tap-to-record, text fallback, mic-denied settings link
- Keep: name field, confirm/cancel, local AsyncStorage persistence

## Deferred (accepted for v1 proto — not store blockers for RC)

- Macro long-press precision mode (spec optional; revisit post-RC)
- Sheet enter/exit motion (`motion.duration.sheet` 280ms)
- Full library browser (IA allows peek-only in v1; list scroll deferred)
- Pixel-verified contrast on physical devices (requires RC device pass)

## Conditions before App Store / Play submission

1. **Real-device walkthrough** on iOS + Android low-end target (Pixel 4a / iPhone SE class)
2. **Captain visual sign-off** on RC build screenshots per state
3. Close UX gate #321 issue after RC device pass confirms zero P0/P1

## Sign-off

> UX + Art Polish Gate **#321 — APPROVED FOR EPIC MERGE (RC track)** as of 2026-09-01.  
> Code-level spec alignment and P0/P1 remediations verified. Device pixel pass remains a **pre-store** condition, not an epic-integration blocker.

**Reviewer:** Shipmates UX gate (automated + remediation)  
**Epic PR:** [#398](https://github.com/saman-mb/agentic-synth/pull/398)
