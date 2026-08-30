# Naming decision: TIMBRE → Tambra

Record of the product rename. Historical design notes stay in `design/REBRAND.md`; this file is the decision log.

## Names

| Name | Role |
| --- | --- |
| **Tambra** | This product: the text-to-sound app. Tagline: *Say it. Hear it.* |
| **Tambra Labs** | Umbrella company for the app suite. |
| **PatchGene** | **Reserved.** Future general-purpose patch-programming agent (any MIDI-capable synth/VST). Do not use for this app. |

## Why Tambra

TIMBRE named the product after the loaded word in sound design. Tambra keeps that root, reads warmer, and checked out collision-free as a product name.

## Domain plan

- Live demo stays on `timbre-synth.netlify.app` until a tambra domain is registered and DNS is pointed.
- Canonical candidates for the owner to register: tambra.app / tambra.audio, tambralabs.ai / tambralabs.com.
- Defensive registration for the reserved agent name: patchgene.ai / patchgene.com.
- After the canonical domain is live, `timbre-synth.netlify.app` should 301 to it, and og:image absolute URLs should follow. That flip is issue #328.

## Out of scope here

GitHub repo name, C++ / plugin `PRODUCT_NAME`, license text (PolyForm Noncommercial is unchanged), and rewriting issue/ADR history.
