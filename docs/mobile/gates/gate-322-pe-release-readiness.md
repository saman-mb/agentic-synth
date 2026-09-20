# Gate #322 — PE Release-Readiness Review

**Epic:** [#294](https://github.com/saman-mb/agentic-synth/issues/294) Expo Mobile App Build (v1)  
**Branch:** `feat/epic-294-expo-mobile-app-build`  
**Date:** 2026-09-01  
**Verdict:** **GO WITH CONDITIONS**

## Local evidence (automated)

| Check | Result |
|-------|--------|
| `nx run mobile:test` | **23/23 pass** |
| `nx run mobile:smoke` | **2/2 pass** |
| `nx run mobile:lint` | **Pass** |
| PR #398 CI | Lint ✅ · Build+Test ✅ · Mobile smoke ✅ · Build wasm ✅ |
| PR #398 deploy | ❌ expected on feature branch (Netlify detects mobile app) |

## Architecture review

| Area | Assessment |
|------|------------|
| ADR-0008 | ✅ Standalone `apps/mobile/package.json`, Metro aliases, no npm workspaces |
| Engine dispose | ✅ `useMobileApp` cleanup: `engine.dispose()`, interval clear, crossfade token guard |
| JS hot paths | ✅ `projectMacroPatch` ~6µs/op, `lerpPatch` ~16µs/op (Node bench) — within 30fps budget |
| Mock vs native | ⚠️ `modules/agsynth/` stub; CI/dev uses mock JSI (`AGSYNTH_FORCE_MOCK=1`) |
| Bundle size | ⏳ Not measured — requires first EAS production build |

## Gate AC status

| AC | Status | Notes |
|----|--------|-------|
| Cold start <2s low-end | ⏳ RC measurement | Mock boot ~0.5ms (Node); device trace required at RC |
| Bundle within budget | ⏳ RC measurement | Provisional: Android AAB ≤45 MB, iOS thin IPA ≤55 MB |
| 10min soak, no leak trend | ⏳ RC measurement | JS dispose patterns sound; native soak needs device |
| Device matrix / crash-free | ⏳ RC plan | iPhone 12 + SE, Pixel 4a + A-series; target ≥99.5% crash-free |
| Go/no-go documented | ✅ | This document |

## Conditions for store submission (post-epic-merge)

1. **Link native JSI** — replace `modules/agsynth/` stub with `src/jsi/` on EAS production profile
2. **Cold-start trace** — instrument tap → audible on Pixel 4a class; must be <2s
3. **EAS production bundle report** — record MB sizes; flag Skia as largest native dep
4. **10min Android soak** — RSS/heap every 60s; no monotonic growth, no thermal kill
5. **Internal TestFlight / Play internal** — 7-day crash-free ≥99.5%
6. **UX gate #321 device pass** — must close before store

## Non-blockers (tracked)

- Dual 30fps loops (scope interval + visualizer rAF) — optional unify post-RC
- Netlify deploy detects `apps/mobile` — fix web deploy config separately
- Nx worktree duplicate graph — tooling hygiene

## Sign-off

> PE Release-Readiness Gate **#322 — GO WITH CONDITIONS** as of 2026-09-01.  
> JS architecture, test coverage, and resource cleanup are **acceptable for epic merge and RC builds**.  
> Device-measured cold start, bundle size, soak, and crash matrix **remain RC exit criteria** before store submission.  
> **Gate closes for epic integration**; re-open only if RC measurements fail budgets.

**Reviewer:** Shipmates PE gate  
**Epic PR:** [#398](https://github.com/saman-mb/agentic-synth/pull/398)
