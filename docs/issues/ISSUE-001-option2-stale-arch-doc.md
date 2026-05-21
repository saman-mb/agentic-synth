---
title: "Option 2 architecture doc references Stable Audio Open 2 — should be Stable Audio 3"
labels: ["option-2", "documentation", "architecture"]
---

## Description

The Option 2 architecture PDF (`docs/agentic-synth-architecture-option2.pdf` in the
actions-runner copy) names **Stable Audio Open 2** as the primary text-to-audio
generation model. However, Stability AI has since released **Stable Audio 3**
(small-music and medium variants), which supersedes it:

- SA3 is a family of fast latent diffusion models for variable-length audio
  generation and editing (up to several minutes).
- SA3 supports inpainting (audio editing/continuation), which Option 2's
  editing layer would benefit from.
- SA3 runs on consumer hardware — "less than a few seconds on a MacBook Pro M4"
  per the model card.
- Open weights under the `stable-audio-community` license.
- Training + inference pipeline: https://github.com/Stability-AI/stable-audio-tools
- Model weights: https://huggingface.co/stabilityai/stable-audio-3-medium
- Paper: arxiv:2605.17991

## What needs to happen

1. Update the Option 2 architecture doc to reference SA3 instead of SAO2.
2. Evaluate whether SA3's inpainting capability changes the Option 2 editing
   layer design (it could replace the need for separate trim/fade tools).
3. Check the `stable-audio-community` license for commercial implications
   (the doc mentions FMA/CC-licensed data — SA3 may have different training data).
4. Consider whether the prompt processor LLM (Phi-4/Llama 3.2 8B) is still
   the right choice, or if SA3's built-in text understanding is sufficient
   for raw prompts.

## Related

- The Option 2 doc exists only as a PDF in the CI runner copy
  (`actions-runner/_work/agentic-synth/agentic-synth/docs/`). It should be
  ported to Markdown in `docs/` alongside the other architecture docs.
