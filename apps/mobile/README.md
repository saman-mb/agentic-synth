# Tambra Mobile (#316)

Expo / React Native scaffold for Tambra — single-screen chrome, JSI engine bridge, Skia visualizer. Part of epic [#294](https://github.com/saman-mb/agentic-synth/issues/294).

## Stack

- **Expo Router** + TypeScript (strict)
- **@shopify/react-native-skia** visualizer (30 fps target)
- **JsiSynthEngine** via `libs/engine-bridge` — native `Agsynth.install()` or mock fallback
- **MobileState** FSM per `docs/mobile/state-machine.md`

## Quick start

```bash
npm ci
npx nx run mobile:test    # Node smoke + unit tests (mock engine)
npx nx run mobile:lint
cd apps/mobile && npm install && npx expo start   # Dev server (install Expo deps locally)
```

### EAS dev client (device audio)

```bash
cd apps/mobile
npx eas-cli build --profile development --platform all
```

Requires `EAS_TOKEN` and Apple/Google credentials. Native JSI module is a stub until `src/jsi/` is linked in prebuild.

## Layout

| Path | Role |
|---|---|
| `app/` | Expo Router entry |
| `src/engine/` | `createMobileEngine`, mock `__AgsynthHost` |
| `src/state/` | MobileState FSM |
| `src/components/` | Chrome per `docs/mobile/ia.md` |
| `assets/demo-patch.json` | Bundled cinematic pad (offline boot) |
| `modules/agsynth/` | Expo native module stub |

## Boot behavior

1. Cold start → `createMobileEngine()` (native or mock)
2. Load `demo-patch.json`, `ensureStarted()`, `noteOn(60)`
3. FSM: `idle` → `hear`, visualizer animates with scope/mock energy

## Deferred (#317–#320)

Voice/text Say flow, macro interaction, variations, Keep — UI stubs only.

## Design tokens

Dark theme from `docs/mobile/tokens.json` (copied to `src/theme/tokens.json`).
