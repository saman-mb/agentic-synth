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

### Flip runbook (after DNS)

Tracked as #328. In-repo prep lives in `netlify.toml` (commented 301). Do not enable it before the custom domain serves 200.

1. Register the product host (tambra.app or tambra.audio) and attach it in the Netlify site domain settings. `timbre-synth.netlify.app` must still answer 200 with no redirect loop.
2. Uncomment the host `[[redirects]]` block in `netlify.toml` (place it above the SPA catch-all) so `timbre-synth.netlify.app` 301s to canonical.
3. Update `og:image` and `twitter:image` in `ui/index.html` from `https://timbre-synth.netlify.app/og-image.png` to the canonical origin. Re-run card validators.
4. Confirm a master push still deploys green.

Until those steps run, og/twitter image URLs stay on `timbre-synth.netlify.app`.

## Out of scope here

GitHub repo name, C++ / plugin `PRODUCT_NAME`, license text (PolyForm Noncommercial is unchanged), and rewriting issue/ADR history.
