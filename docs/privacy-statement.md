# Privacy and Data-Handling Statement

**Last updated: September 2026**

Tambra (Agentic Synth) is **local-first for audio DSP**, but **not offline-only** for AI features. Patch generation and (where enabled) speech-to-text call **Google Gemini**. This statement covers the desktop/plugin build, the Netlify web demo, and the paid mobile path.

There is **no account system**. We do not ask you to register an email or password with Tambra.

Related: [App Store / Play nutrition labels](mobile/privacy-nutrition-labels.md) · [Entitlement runbook](runbooks/entitlement.md) · [LICENSE](../LICENSE) (PolyForm Noncommercial 1.0.0)

---

## Summary by product surface

| Surface | Mic / audio | Prompts / briefs | What we store server-side |
|---------|-------------|------------------|---------------------------|
| **Desktop / plugin** | Optional push-to-talk: PCM is captured in the UI; when `GEMINI_KEY` is set, utterance audio is sent to Gemini for transcription. Synth output audio stays on-device. | Prefer local `llama-server` when available; otherwise (and for enhancement) prompts go to Gemini with your local `GEMINI_KEY`. | Nothing of yours on our servers — the plugin talks to Google (and optionally your LAN llama server) directly. |
| **Web demo** | **No mic required.** Push-to-talk / STT is **disabled** in the browser demo. | Text prompts `POST` to Netlify `/api/brief` and `/api/generate`; the function calls Gemini with server-side `GEMINI_KEY`. | Rate-limit / quota **counters** keyed by IP or optional `X-Device-Id` (not prompt text). Short-lived in-memory brief cache per function isolate. |
| **Mobile (paid)** | Optional voice capture for prompts (tap-to-record in the mobile UX contract). Text-only works without a mic. Remote STT, when used, leaves the device for transcription; confirmed text is what is sent to generate. | Same Netlify `/api/brief` + `/api/generate` as the demo, with `Authorization: Bearer` entitlement JWT. | Receipt validated in-request (not retained by our entitlement handler). JWT is stateless. Rate-limit counters keyed by paid subject (`sub` from the token). |

---

## 1. Desktop / plugin (local)

### What stays on your machine

- **DSP and playback** — oscillators, filters, effects, and rendered audio run in-process (JUCE). We do not upload synth output or bounce WAVs.
- **Presets / session UI state** — kept in the app’s local data paths (and optional local telemetry logs if you enable them).
- **Optional local LLM** — if you run `llama-server` yourself, patch generation can use that HTTP endpoint on localhost or your LAN ([local inference](local-inference.md)). That traffic does not go through Tambra’s Netlify functions.

### Cloud paths (when configured)

With a local `GEMINI_KEY`:

1. **Prompt enhancement + patch generation** — after a failed/unavailable local llama path, the plugin falls back to Google’s Generative Language API (`generativelanguage.googleapis.com`) via `PromptEnhancer` / `GeminiSampler`.
2. **Push-to-talk STT** — `PushToTalk` captures microphone PCM (16 kHz mono). The native bridge (`push_audio_pcm`) sends that utterance to **Gemini audio understanding** (`GeminiSTT`) and returns a transcript into the chat field. Raw mic buffers are not written to our cloud; they are used for that transcription round-trip. If `GEMINI_KEY` is unset, STT is disabled and no audio is uploaded.

Whisper.cpp scaffolding exists in-tree but is **not** the live STT path today.

### Local telemetry (opt-in)

If enabled, metrics such as generation latency and error counts stay in a **local** JSON log (for example under `~/.local/share/agentic-synth/telemetry/` on macOS/Linux). That panel’s “stays local” copy refers to this store — it is separate from Gemini calls above.

---

## 2. Web demo (Netlify)

The browser demo uses the same React UI with a shim: generation is `fetch` to Netlify Functions, audio is WASM DSP in the browser.

### Prompts → Gemini

1. Your producer prompt is `POST`ed as JSON to **`/api/brief`** (enhancer).
2. The brief (or sanitized prompt) is then `POST`ed to **`/api/generate`** (patch generator).
3. Both functions call Gemini server-side using the site’s **`GEMINI_KEY`** (never shipped to the browser).

Prompts are processed to produce a brief and a patch. They are **not** written to a Tambra user database. Operational detail that is not long-term prompt storage:

- **Brief cache** — in-memory LRU inside a function isolate (canonicalized sanitized prompt → brief). Cold starts clear it.
- **Google** — prompts (and model outputs) are processed under Google’s Gemini API terms. On free-tier / consumer API usage, Google may retain or use prompts per those terms; consult [Google’s Gemini API / generative AI privacy documentation](https://ai.google.dev/) for the current policy. Tambra does not control Google’s retention.

### Microphone

**Not required** for the web demo. Push-to-talk STT is explicitly disabled in the demo shim (`push_audio_pcm` returns a “speech-to-text disabled in web demo” transcript). Text prompts only.

### Rate limits and identity (demo tier)

Before Gemini work, `/api/brief` and `/api/generate` resolve an identity ([`identity.mts`](../netlify/functions/lib/identity.mts)):

1. `Authorization: Bearer …` (paid — unused by the anonymous demo UI)
2. Else optional **`X-Device-Id`** (UUID v4) → subject `dev:{uuid}`
3. Else **client IP** (`x-nf-client-connection-ip`, else `x-forwarded-for`) → subject `ip:{addr}`

Durable stores (Netlify Blobs by default) keep **request counters** for minute/day windows and related quota/ops metrics keyed by that subject — **not** prompt bodies, not audio, not names. Shared NAT IPs share a demo bucket; clients may send `X-Device-Id` to avoid that collision. See [abuse-429](runbooks/abuse-429.md) and [gemini-spend](runbooks/gemini-spend.md).

Browser `localStorage` keys under `agentic-synth.demo.*` (presets, dictionary, telemetry toggle, etc.) stay in **your browser** only.

---

## 3. Mobile (paid path)

Mobile UX is documented under [`docs/mobile/`](mobile/README.md). Generation uses the same Netlify APIs as the web demo, with entitlement.

### Microphone

- **Text prompts** — no mic needed.
- **Voice prompts** — the OS microphone may be used (tap-to-record). Audio is buffered on-device for transcription. When remote STT is used, that audio leaves the device for transcription; you confirm/edit the text before **Send**. Mic permission denied soft-degrades to text ([input](mobile/input.md)).
- **Synth / preview audio** is not uploaded as a library of recordings.

### Entitlement (no accounts)

1. Client `POST /api/entitlement` with a store **receipt** + platform (`ios` / `android`).
2. Server validates the receipt (stub or Apple `verifyReceipt`; Play verify residual) and returns a short-lived **HS256 JWT** (`token`, `expires_at`, `tier`). Claims are only **`sub`**, **`tier: "paid"`**, **`exp`** ([entitlement runbook](runbooks/entitlement.md)).
3. Client sends `Authorization: Bearer <token>` on `/api/brief` and `/api/generate` for the paid rate tier.

**Retention (our systems):**

| Data | Retention |
|------|-----------|
| Store receipt bytes | Validated during the entitlement request; **not** persisted by the entitlement issuer after the response. |
| Entitlement JWT | Stateless; verified with `ENTITLEMENT_SIGNING_KEY`. Default TTL **3600 s** (`ENTITLEMENT_TOKEN_TTL_SECONDS`). Client may keep the token until expiry/refresh. |
| Rate-limit subject | Counters keyed by paid `sub` (e.g. `ios:<original_transaction_id>`), same counter store as the demo — not full receipts or prompts. |
| Prompts / briefs | Same as web demo: processed via Gemini for the request; no Tambra prompt archive. |

Apple / Google retain purchase records under **their** store policies; that is outside Tambra’s database (we have none for users).

---

## 4. What we do not do

- No Tambra login, email capture, or customer profile store.
- No analytics SDKs, tracking pixels, or crash reporters that phone home (beyond the Gemini / store / Netlify paths above).
- No sale of personal data.
- No uploading of bounced WAVs or continuous ambient mic recording.

---

## 5. License framing (PolyForm Noncommercial)

The software is licensed under the **[PolyForm Noncommercial License 1.0.0](../LICENSE)**. Noncommercial use is permitted under that license; commercial use requires a separate license from the copyright holder. License terms are about **use rights**, not a privacy consent — they do not replace this statement. Inquiries: [github.com/saman-mb](https://github.com/saman-mb).

---

## 6. Deletion and privacy contact

Tambra does not host user accounts or a prompt archive you can “log into.” To request deletion of any residual operational data we control (for example rate-limit subject keys associated with your IP, device id, or paid `sub`), or to ask privacy questions:

1. **Open a GitHub issue** on [saman-mb/agentic-synth](https://github.com/saman-mb/agentic-synth/issues) (preferred), or
2. Contact the maintainer via **[github.com/saman-mb](https://github.com/saman-mb)** (no public maintainer email is published in the README).

For data held only by **Google** (Gemini) or **Apple / Google Play** (purchases), use those providers’ own tools and policies.

You can clear local desktop telemetry/session data by deleting the app data directory, and clear web-demo `localStorage` in the browser.
