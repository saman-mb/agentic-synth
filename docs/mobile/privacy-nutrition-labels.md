# App Store / Play privacy nutrition labels

**Last updated: September 2026**  
**Source of truth:** [Privacy statement](../privacy-statement.md)

Paste-ready answers for Apple App Privacy and Google Play Data safety. Keep this file aligned with the privacy statement whenever flows change. **Console labels are copy for the captain to paste** — submitting them in App Store Connect / Play Console is outside this repo.

Assumes the **paid mobile** path: mic optional for voice prompts, prompts via Netlify → Gemini, entitlement receipt → JWT, no Tambra accounts.

---

## Data types (yes / no)

| Category | Collected? | Linked to identity? | Used for tracking? | Notes |
|----------|------------|---------------------|--------------------|-------|
| **Audio data** (microphone) | **Yes, if** the user uses voice input | No Tambra account; may be sent to STT/Gemini for that utterance | No | Not required for text-only. Not continuous recording. Synth playback not uploaded as a library. |
| **User content** (prompts / briefs) | **Yes** | No account; processed per request | No | `POST /api/brief`, `/api/generate` → Gemini. No Tambra prompt archive. |
| **Purchases** | **Yes** (store receipt for entitlement) | Subject derived from transaction id / stub id in JWT `sub` | No | Receipt validated in-request; not retained by our entitlement issuer after response. Stores keep their own purchase records. |
| **Identifiers** | **Yes** (operational) | Rate-limit / entitlement subject only | No | Paid: JWT `sub`. Demo-like fallback: IP or optional `X-Device-Id`. Not used for advertising. |
| **Contact info** (email, name, phone) | **No** | — | — | No Tambra accounts. |
| **Location** | **No** | — | — | |
| **Health / Sensitive info** | **No** | — | — | |
| **Contacts / Photos / Files** (beyond OS store receipt APIs) | **No** | — | — | |
| **Diagnostics / Crash** (third-party) | **No** | — | — | No external crash/analytics SDK declared for mobile v1. |
| **Advertising data** | **No** | — | — | |

---

## Apple App Privacy (questionnaire direction)

| Question | Answer |
|----------|--------|
| Do you or your third-party partners collect data? | **Yes** — prompts (and mic audio when voice STT is used) processed via Google Gemini; purchase receipt validated with Apple; operational rate-limit counters on our Netlify backend. |
| Privacy Nutrition Label — **Audio Data** | Collect if voice used; purpose: App Functionality (speech → text for sound prompts). Not used for tracking. |
| **User Content** | Product Interaction / App Functionality — generate synth patches from text. |
| **Purchases** | App Functionality — unlock paid API tier via entitlement token. |
| **Identifiers** (Device ID / other) | App Functionality — abuse prevention / rate limits only if sent (`X-Device-Id`) or as paid `sub`; IP may be used on server when no device id / bearer. |
| Data used to track you? | **No**. |
| Data linked to user identity? | **No** Tambra user identity; ephemeral/operational subjects only. |
| Privacy policy URL | Point at the published `docs/privacy-statement.md` (or rendered site copy of the same text). |

---

## Google Play Data safety (questionnaire direction)

| Section | Answer |
|---------|--------|
| Does your app collect or share user data? | **Yes** |
| Collected: **Audio** | Yes (optional voice prompts) — shared with STT/Gemini when remote transcription is used |
| Collected: **App activity / in-app messages** (or User-generated content) | Yes — text prompts/briefs for generation |
| Collected: **App info and performance** | Only if you later add crash/analytics — **currently No** for third-party diagnostics |
| Collected: **Financial info / Purchase history** | Yes — receipt for entitlement validation; shared with Apple/Google as required by billing APIs |
| Collected: **Device or other IDs** | Yes — rate limiting / paid subject |
| Data encrypted in transit? | **Yes** (HTTPS to Netlify / Gemini / store APIs) |
| Can users request deletion? | **Yes** — see privacy statement §6 (GitHub issue / maintainer). Note: no account store; limited operational keys. |
| Data shared for advertising? | **No** |
| Ephemeral processing | Mic/prompt handling for a single generate/STT request; entitlement receipt not stored by our issuer after issue |

---

## Consistency checklist (before store submission)

- [ ] Mic permission string matches “voice prompts / describe a sound,” not ambient recording.
- [ ] Privacy policy URL serves the same claims as [privacy-statement.md](../privacy-statement.md).
- [ ] Nutrition labels still match if STT vendor, entitlement retention, or identity headers change.
- [ ] Do **not** claim “data never leaves the device” while Gemini / Netlify paths are live.

## Residual (captain)

Actual App Store Connect / Play Console form submission and localization of permission strings remain a release-ops task.
