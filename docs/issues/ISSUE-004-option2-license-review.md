---
title: "Option 2 license implications — Stable Audio 3 uses stable-audio-community license"
labels: ["option-2", "legal", "compliance"]
---

## Description

The Option 2 architecture doc recommends **Stable Audio Open 2**, which was
trained on Free Music Archive data (CC-licensed). However, **Stable Audio 3**
(the current recommended model) uses the `stable-audio-community` license
and includes components redistributed under the Gemma Terms of Use (including
use restrictions in Section 3.2).

This has implications for TIMBRE's distribution model:

### What needs review

1. **Can we distribute SA3 model weights with the plugin?** Or must users
   download them separately (like the current llama.cpp setup)?
2. **Can TIMBRE be sold commercially** with SA3 as the backend? What are the
   revenue-sharing or licensing requirements?
3. **Does the Gemma Terms Section 3.2** restrict certain use cases (e.g.,
   competitive AI products, certain industries)?
4. **Alternative**: MusicGen (Meta) uses MIT-licensed weights with fewer
   restrictions. Trade-off: lower quality for non-musical sounds, mono only,
   max 30s.
5. **Another alternative**: Using SA3 for inference but keeping the inference
   server as a separate download (like the current llama.cpp pattern) may
   simplify licensing.

### Risk

If the `stable-audio-community` license prohibits commercial use or requires
revenue sharing, we may need to:
- Use a different model (MusicGen, AudioGen)
- Keep generation as an optional BYO-model feature
- Offer a cloud inference tier with proper licensing

Needs a legal review before committing to SA3 as the core engine.
