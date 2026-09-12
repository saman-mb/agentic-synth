# ADR-0008: Nx apps + libs with tagged module boundaries

## Status

Accepted

## Date

2026-08-31

## Deciders

- Tambra maintainers

## Context and Problem Statement

Tambra needs one JavaScript workspace that can feed more than one app: the
Vite web demo today, a React Native / Expo app later (#316), and a future
C++/WASM/JSI engine behind the same JS seam (#292). A single `packages/core`
tree (the original #291 shape) would not give a project graph, per-app
targets, or lintable dependency direction.

#302 (`packages/core` pnpm layout) is superseded by this decision and by the
amended #291 (Nx libs, not a second package manager).

## Decision Drivers

- One graph, many apps — `nx affected` and per-project `build` / `lint` / `test`.
- Dependency direction must be enforced in-repo, not by convention.
- Netlify esbuild cannot resolve TypeScript path aliases; function bundles
  stay on relative imports of leaf files.
- No npm workspaces — a nested `apps/web/package.json` already confuses
  Netlify CLI into skipping repo-root functions.

## Considered Options

- pnpm workspaces + `packages/core` (#302 / original #291)
- Nx apps + tagged libs (this epic)
- Keep everything under `apps/web/src` and copy later

## Decision Outcome

Chosen option: **Nx apps + tagged libs**, consumed via `tsconfig.base.json`
paths and Vite `resolve.alias` (`@agentic-synth/<lib>`). No `workspaces`
field. Source libs have no `package.json`.

### Tag scheme

| Tag | Who | May depend on |
| --- | --- | --- |
| `type:app` + `scope:web` | `apps/web` | `type:lib` |
| `layer:types` | `libs/shared-types` | `layer:types` only |
| `layer:data` | `libs/data` | `layer:types`, `layer:data` |
| `layer:engine` | `libs/engine-bridge` | `layer:types`, `layer:data`, `layer:engine` |
| `layer:feature` | `libs/codec`, `libs/prompt`, `libs/modval` | `layer:types`, `layer:data`, `layer:feature` |

Feature libs do **not** depend on `engine-bridge`. Web composes both.
`@nx/enforce-module-boundaries` in root `eslint.config.js` is the gate;
`scripts/assert-reversed-import.mjs` proves a lib→app import fails lint
(the Nx plugin needs a project graph first — `nx show projects` in that
script).

### Engine attach (paved road for #292 / #316)

`libs/engine-bridge` exports `SynthEngine` + `createSynthEngine()` (WebAudio
today). WASM (web) and JSI (React Native) land as additional implementations
of that type; the factory swaps, the UI does not. `apps/mobile` is not
authored here — add it as an Nx app that imports `@agentic-synth/engine-bridge`
and `@agentic-synth/shared-types` the same way `apps/web` does.

### How to add `apps/mobile` later

1. Scaffold an Nx app under `apps/mobile` with tags `type:app, scope:mobile`.
2. Point it at the same path aliases; do not add npm workspaces.
3. Implement `SynthEngine` for JSI in engine-bridge (or a sibling file);
   do not import `apps/web`.
4. Keep Netlify `included_files` as an explicit list — never glob `libs/**`.

## Pros and Cons of the Options

### Nx tagged libs

- Pros: Graph, `nx affected`, lintable layers, one alias style for a future
  mobile app, engine seam exists before WASM/JSI.
- Cons: Path aliases plus relative Netlify imports (two styles); ESLint
  plugin no-ops without an Nx project graph.

### pnpm `packages/core`

- Pros: Familiar package names on disk.
- Cons: Second package manager; no `nx affected`; Netlify CLI treats nested
  packages as extra sites and can skip functions.

## Links

- Epic #329, stories #330–#335
- Amended #291, superseded #302
- Engine epic #292, Expo #316
- `eslint.config.js` `depConstraints`, `tsconfig.base.json`, `libs/engine-bridge/README.md`
