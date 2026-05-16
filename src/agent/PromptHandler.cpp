#include "agent/PromptHandler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string_view>

#include "agent/KnobBridge.h"
#include "agent/PatchAugmenter.h"
#include "mapper/ArchetypeLibrary.h"
#include "mapper/ArchetypeRetriever.h"

namespace agentic_synth::agent {

namespace {

// ── Phase 22: relative-language detection ────────────────────────────────────
//
// Static keyword set drawn from the system-prompt §5.3 directional dictionary
// plus the everyday English comparatives producers actually type into the
// chat ("more wobble", "weirder", "a bit ominous"). Match is case-insensitive
// substring with word-boundary guards on both sides so "softer" matches but
// "software" does not. We do NOT NLP this — a keyword sweep is enough; the
// LLM handles the actual semantics under §5.3.
constexpr std::array<std::string_view, 70> kRelativeKeywords = {
    "darker", "brighter", "deeper", "wider", "tighter", "punchier", "heavier",
    "lighter", "weirder", "softer", "thicker", "thinner", "fatter", "fuller",
    "hollow", "hollower", "smoother", "harsher", "snappier", "slower", "faster",
    "longer", "shorter", "drier", "wetter", "cleaner", "dirtier", "gentler",
    "meaner", "lusher", "sparser", "bigger", "smaller", "dimmer", "ominous",
    "evil", "ominouser",
    // Multi-word fragments — substring match handles these.
    "more ", "less ", "also ", "and add", "with more", "with less", "less of",
    "more of", "also make it", " but ", " just ", "slightly", "a bit", "way more",
    "way less", "evil-er", "more X", "less Y",
    // Single-token comparatives that aren't in the dictionary but signal intent.
    "stronger", "weaker", "quieter", "louder", "warmer", "cooler", "drier",
    "moodier", "shimmerier", "deeper", "crispier", "crisper", "rounder",
};

// Lowercase ASCII view of `s` — allocates; only called on the cold prompt
// path (one or two times per user submission), never on the audio thread.
std::string toLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isWordChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Word-boundary-aware substring match. Multi-token keywords (those containing
// a space) skip the boundary check on the side adjacent to the space because
// they already encode their own boundary.
bool containsWord(const std::string& haystack, std::string_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size())
        return false;
    const bool needsLeftBoundary = needle.front() != ' ';
    const bool needsRightBoundary = needle.back() != ' ';
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        const bool leftOk = !needsLeftBoundary || pos == 0 || !isWordChar(haystack[pos - 1]);
        const size_t end = pos + needle.size();
        const bool rightOk = !needsRightBoundary || end >= haystack.size() || !isWordChar(haystack[end]);
        if (leftOk && rightOk)
            return true;
        ++pos;
    }
    return false;
}

// ── Phase 22: PatchStruct → JSON for the refinement wrapper ──────────────────
//
// Round-trip format matches GrammarSampler::parse_patch_json exactly so a
// patch we send to the LLM can be re-parsed if echoed back. Hand-rolled
// (rather than juce::JSON) so the agent layer stays JUCE-free. Floats use
// "%g" — short, lossy-but-round-trippable for the value ranges the engine
// uses. Enums emit their canonical string spellings (matches parse_osc_type
// etc.).
const char* oscTypeName(OscType t) noexcept {
    switch (t) {
    case OscType::Sine: return "Sine";
    case OscType::Triangle: return "Triangle";
    case OscType::Sawtooth: return "Sawtooth";
    case OscType::Square: return "Square";
    case OscType::Pulse: return "Pulse";
    case OscType::Wavetable: return "Wavetable";
    case OscType::FM: return "FM";
    case OscType::Noise: return "Noise";
    }
    return "Sawtooth";
}

const char* filterTypeName(FilterType t) noexcept {
    switch (t) {
    case FilterType::LowPass: return "LowPass";
    case FilterType::HighPass: return "HighPass";
    case FilterType::BandPass: return "BandPass";
    case FilterType::Notch: return "Notch";
    case FilterType::Peak: return "Peak";
    }
    return "LowPass";
}

const char* lfoWaveformName(LfoWaveform w) noexcept {
    switch (w) {
    case LfoWaveform::Sine: return "Sine";
    case LfoWaveform::Triangle: return "Triangle";
    case LfoWaveform::Sawtooth: return "Sawtooth";
    case LfoWaveform::Square: return "Square";
    case LfoWaveform::SampleAndHold: return "SampleAndHold";
    }
    return "Sine";
}

const char* lfoTargetName(LfoTarget t) noexcept {
    switch (t) {
    case LfoTarget::None: return "None";
    case LfoTarget::Pitch: return "Pitch";
    case LfoTarget::FilterCutoff: return "FilterCutoff";
    case LfoTarget::Amplitude: return "Amplitude";
    case LfoTarget::Pan: return "Pan";
    case LfoTarget::WavetablePos: return "WavetablePos";
    case LfoTarget::FmRatio: return "FmRatio";
    }
    return "None";
}

std::string patchToJsonString(const PatchStruct& p) {
    std::ostringstream o;
    o << "{";
    o << "\"version\":" << p.version << ",";
    o << "\"patch_id\":" << p.patch_id << ",";
    o << "\"osc\":[";
    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& osc = p.osc[i];
        if (i > 0) o << ",";
        o << "{\"type\":\"" << oscTypeName(osc.type) << "\","
          << "\"semitone_offset\":" << osc.semitone_offset << ","
          << "\"detune_cents\":" << osc.detune_cents << ","
          << "\"wavetable_pos\":" << osc.wavetable_pos << ","
          << "\"fm_ratio\":" << osc.fm_ratio << ","
          << "\"fm_depth\":" << osc.fm_depth << ","
          << "\"volume\":" << osc.volume << ","
          << "\"pan\":" << osc.pan << ","
          << "\"pulse_width\":" << osc.pulse_width << ","
          << "\"enabled\":" << (osc.enabled ? "true" : "false") << "}";
    }
    o << "],";
    o << "\"filter\":{"
      << "\"type\":\"" << filterTypeName(p.filter.type) << "\","
      << "\"cutoff_hz\":" << p.filter.cutoff_hz << ","
      << "\"resonance\":" << p.filter.resonance << ","
      << "\"env_mod\":" << p.filter.env_mod << ","
      << "\"key_track\":" << p.filter.key_track << ","
      << "\"drive\":" << p.filter.drive << "},";
    auto envOut = [&](const char* key, const EnvParams& e) {
        o << "\"" << key << "\":{"
          << "\"attack_s\":" << e.attack_s << ","
          << "\"decay_s\":" << e.decay_s << ","
          << "\"sustain\":" << e.sustain << ","
          << "\"release_s\":" << e.release_s << "}";
    };
    envOut("filter_env", p.filter_env); o << ",";
    envOut("amp_env", p.amp_env); o << ",";
    o << "\"lfo\":[";
    for (int i = 0; i < kMaxLfos; ++i) {
        const auto& l = p.lfo[i];
        if (i > 0) o << ",";
        o << "{\"waveform\":\"" << lfoWaveformName(l.waveform) << "\","
          << "\"target\":\"" << lfoTargetName(l.target) << "\","
          << "\"rate_hz\":" << l.rate_hz << ","
          << "\"depth\":" << l.depth << ","
          << "\"phase_offset\":" << l.phase_offset << ","
          << "\"bpm_sync\":" << (l.bpm_sync ? "true" : "false") << "}";
    }
    o << "],";
    o << "\"reverb\":{"
      << "\"size\":" << p.reverb.size << ","
      << "\"damping\":" << p.reverb.damping << ","
      << "\"width\":" << p.reverb.width << ","
      << "\"mix\":" << p.reverb.mix << "},";
    o << "\"delay\":{"
      << "\"time_s\":" << p.delay.time_s << ","
      << "\"feedback\":" << p.delay.feedback << ","
      << "\"mix\":" << p.delay.mix << ","
      << "\"stereo\":" << p.delay.stereo << ","
      << "\"bpm_sync\":" << (p.delay.bpm_sync ? "true" : "false") << "},";
    o << "\"master_gain\":" << p.master_gain << ","
      << "\"portamento_s\":" << p.portamento_s << ","
      << "\"voice_count\":" << static_cast<int>(p.voice_count);
    o << "}";
    return o.str();
}

// Build the refinement-mode wrapper that the LLM receives instead of the raw
// user prompt. Mirrors the §5.3 contract: "here's the previous patch,
// here's what the user originally asked for, here's the new directive —
// nudge, don't restart." The exact template wording is duplicated here AND in
// system-prompt.md §5.3 on purpose: the prompt repeats the instruction
// inline so even a model that ignores the system prompt sees it again.
std::string buildRefinementWrapper(const std::string& newPrompt, const PatchStruct& previousPatch,
                                   const std::string& previousPrompt) {
    std::ostringstream w;
    w << "REFINEMENT MODE.\n\n"
      << "Previous patch (do NOT regenerate from scratch):\n"
      << patchToJsonString(previousPatch) << "\n\n"
      << "Previous user prompt: \"" << previousPrompt << "\"\n\n"
      << "User now says: \"" << newPrompt << "\"\n\n"
      << "Apply the refinement contract from §5.3. Start from the previous patch.\n"
      << "Shift in the direction the user names. Preserve everything else. Output\n"
      << "the full patch JSON as usual.";
    return w.str();
}

} // namespace

bool PromptHandler::isRelativePrompt(const std::string& prompt) noexcept {
    if (prompt.empty())
        return false;
    const std::string lower = toLowerAscii(prompt);
    for (const auto& kw : kRelativeKeywords) {
        if (containsWord(lower, kw))
            return true;
    }
    return false;
}

PatchStruct PromptHandler::submitPrompt(const std::string& prompt) {
    // Reset stream parser so field-complete callbacks from a prior LLM call
    // don't bleed into this submission's heuristic patch.
    streamParser_.reset();
    // Issue #65/#68: heuristic dispatched < 200 ms; semantic layer refines in place.
    PatchStruct patch = pipeline_.submit(prompt);
    if (semanticMapper_.apply(prompt, patch) > 0)
        pipeline_.injectPatch(patch);
    return patch;
}

void PromptHandler::refinePatch(const PatchStruct& llmPatch) { pipeline_.refinePatch(llmPatch); }

std::optional<PatchStruct> PromptHandler::generateLlmPatch(const std::string& prompt, uint32_t patch_id,
                                                           std::optional<PatchStruct> previousPatch,
                                                           std::optional<std::string> previousPrompt) {
    // Phase 22: when the user's new prompt is comparative ("darker", "more
    // wobble") AND we have a prior patch to nudge from, wrap the request in
    // a refinement frame that pins the previous patch into the LLM context.
    // §5.3 of system-prompt.md tells the model how to behave when it sees
    // this frame: read the prior patch, identify the named direction, shift
    // it, preserve everything else. Without this wrapping the model
    // regenerates from scratch and erases parameters the user explicitly
    // wanted to keep.
    //
    // The same wrapped prompt goes to BOTH samplers (local llama.cpp and
    // Gemini fallback) so refinement behaviour is consistent regardless of
    // which backend produced the previous patch.
    const bool refinement = isRelativePrompt(prompt) && previousPatch.has_value();
    const std::string promptForSampler =
        refinement ? buildRefinementWrapper(prompt, *previousPatch, previousPrompt.value_or("")) : prompt;
    if (refinement) {
        std::cerr << "[PromptHandler] refinement mode (prev prompt='" << previousPrompt.value_or("")
                  << "', new='" << prompt << "')\n";
    }

    // Try the local llama.cpp /completion server first; on failure (server
    // not running, connect refused, malformed response) fall back to the
    // Gemini API when a GEMINI_KEY was discovered at startup. Both paths
    // share GrammarSampler::parse_patch_json + validate_patch so the
    // resulting PatchStruct is uniformly safe to refine into the pipeline.
    // Phase 23 — guardrail. When NOT in refinement mode, post-process the
    // LLM's patch to enforce system-prompt §0 rules 10/12 (3-osc default +
    // noise-only ban) before it reaches the engine. The model honours these
    // rules ~80% of the time; the augmenter is the server-side safety net
    // for the rest. In refinement mode we deliberately skip augmentation —
    // the user is nudging an existing topology and would not appreciate the
    // augmenter re-layering on top of an explicit shift.
    auto applyGuardrail = [&](PatchStruct& patch) {
        if (!refinement)
            augmentPatch(patch, prompt);
    };

    auto result = sampler_.generate(promptForSampler, patch_id);
    if (result) {
        std::cerr << "[PromptHandler] LLM path=local-llama.cpp ok"
                  << (refinement ? " (refinement)" : "") << "\n";
        applyGuardrail(*result);
        refinePatch(*result);
        return result;
    }

    if (gemini_.enabled()) {
        std::cerr << "[PromptHandler] local llama.cpp unavailable; trying Gemini fallback\n";
        result = gemini_.generate(promptForSampler, patch_id);
        if (result) {
            std::cerr << "[PromptHandler] LLM path=gemini ok"
                      << (refinement ? " (refinement)" : "") << "\n";
            applyGuardrail(*result);
            refinePatch(*result);
            return result;
        }
        std::cerr << "[PromptHandler] LLM path=gemini failed; no patch produced\n";
    } else {
        std::cerr << "[PromptHandler] local llama.cpp unavailable and GEMINI_KEY unset; no patch produced\n";
    }

    // ── Phase 34a: minimum-viable RAG fallback ──────────────────────────────
    //
    // Both LLM paths failed. Rather than hand back nullopt and let the worker
    // ship the bare heuristic patch from PrePatchPipeline, we retrieve the
    // best-matching archetype from the curated library (substring score over
    // descriptor tags) and return that. The retrieved patch still flows
    // through applyGuardrail (PatchAugmenter) so refinement-mode bypass and
    // §0 layering rules still apply — exactly the same pipeline as a
    // successful LLM response.
    //
    // The retriever NEVER returns nullptr (it falls back to the
    // `default_init` archetype on no-match), so this branch always produces
    // a patch when the library is non-empty.
    if (const auto* arch = mapper::ArchetypeRetriever::retrieve(prompt)) {
        std::string tagList;
        for (size_t i = 0; i < arch->tags.size(); ++i) {
            if (i > 0)
                tagList += ", ";
            tagList += arch->tags[i];
        }
        std::cerr << "[PromptHandler] LLM unavailable; using retrieved archetype '"
                  << arch->name << "' (tags: " << tagList << ")\n";
        PatchStruct patch = arch->patch;
        patch.patch_id = patch_id;
        applyGuardrail(patch);
        refinePatch(patch);
        return patch;
    }
    return std::nullopt;
}

void PromptHandler::applyGuardrailIfNotRefinement(PatchStruct& patch, const std::string& prompt,
                                                  bool hasPreviousPatch) noexcept {
    // Phase 31 — same refinement-skip rule as generateLlmPatch::applyGuardrail.
    // Caller passes hasPreviousPatch so we don't have to plumb the optional
    // through (WebUiComponent already snapshots priorPatch under its mutex).
    if (isRelativePrompt(prompt) && hasPreviousPatch)
        return;
    augmentPatch(patch, prompt);
}

void PromptHandler::feedChunk(std::string_view chunk) { streamParser_.feedChunk(chunk); }

PromptHandler::SplitPrompt PromptHandler::splitSystemPrompt(const std::string& full) {
    SplitPrompt out;
    if (full.empty())
        return out;

    // Locate §3 ("## 3. DSP Archetypes ...") and §4 ("## 4. ...") header
    // anchors. If either is missing the file isn't shaped like our
    // system-prompt.md — fall back to "rails = full, no archetypes".
    const std::string kHdr3 = "\n## 3.";
    const std::string kHdr4 = "\n## 4.";
    const auto pos3 = full.find(kHdr3);
    const auto pos4 = full.find(kHdr4, pos3 == std::string::npos ? 0 : pos3);
    if (pos3 == std::string::npos || pos4 == std::string::npos || pos4 <= pos3) {
        out.rails = full;
        return out;
    }

    // Rails = pre-§3 + post-§3 (skip the leading newline so we don't dup
    // blank lines).
    out.rails.reserve(full.size());
    out.rails.append(full, 0, pos3);
    out.rails.append("\n"); // single separator between the slice halves
    out.rails.append(full, pos4 + 1, std::string::npos);

    // Walk §3 looking for "<N>. **<Name>** —" recipe entries. The §3.0
    // keyword-lock preamble is kept as one synthetic "keyword-lock" entry
    // so a future caller can choose to always append it.
    const std::string body = full.substr(pos3, pos4 - pos3);

    auto lower = [](std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    // Find each numbered recipe header. Regex would be cleaner but we want
    // zero new deps — a manual scan over "\nN. **" is enough.
    std::size_t i = 0;
    while (i < body.size()) {
        // Look for newline followed by digit(s) + ". **".
        const auto nl = body.find('\n', i);
        if (nl == std::string::npos)
            break;
        std::size_t j = nl + 1;
        // Must start with at least one digit.
        if (j >= body.size() || !std::isdigit(static_cast<unsigned char>(body[j]))) {
            i = nl + 1;
            continue;
        }
        std::size_t k = j;
        while (k < body.size() && std::isdigit(static_cast<unsigned char>(body[k])))
            ++k;
        if (k + 4 >= body.size() || body[k] != '.' || body[k + 1] != ' ' ||
            body[k + 2] != '*' || body[k + 3] != '*') {
            i = nl + 1;
            continue;
        }
        // Extract the name between the `**...**` delimiters.
        const std::size_t name_start = k + 4;
        const auto name_end = body.find("**", name_start);
        if (name_end == std::string::npos)
            break;
        const std::string name = body.substr(name_start, name_end - name_start);

        // Recipe text runs until the next newline-then-digit-then-period
        // header, or end of §3.
        std::size_t recipe_end = body.size();
        std::size_t scan = name_end;
        while (scan < body.size()) {
            const auto nnl = body.find('\n', scan + 1);
            if (nnl == std::string::npos)
                break;
            std::size_t m = nnl + 1;
            if (m < body.size() && std::isdigit(static_cast<unsigned char>(body[m]))) {
                // Confirm the "N. **" shape before treating it as a header.
                std::size_t mm = m;
                while (mm < body.size() && std::isdigit(static_cast<unsigned char>(body[mm])))
                    ++mm;
                if (mm + 4 < body.size() && body[mm] == '.' && body[mm + 1] == ' ' &&
                    body[mm + 2] == '*' && body[mm + 3] == '*') {
                    recipe_end = nnl;
                    break;
                }
            }
            scan = nnl;
        }

        const std::string recipe = body.substr(j, recipe_end - j);
        // Keyword = lowercased first word of the name (e.g. "Sub bass" → "sub").
        // Cheap, and matches the keyword-lock dictionary's vocabulary. A
        // follow-up phase can swap in multi-keyword matching once we wire
        // the keyword harness.
        std::string keyword;
        for (char c : name) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ',')
                break;
            keyword += c;
        }
        if (!keyword.empty())
            out.archetypes.emplace_back(lower(keyword), recipe);

        i = recipe_end;
    }
    return out;
}

std::string PromptHandler::buildSystemPrompt(const std::string& userPrompt) const {
    const std::string& base =
        sampler_.systemPrompt().empty()
            ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
            : sampler_.systemPrompt();

    // Phase 33 — rails-only path is wired but OFF by default (the flag
    // `use_rails_only_` is set false on construction). When flipped, we
    // strip §3 and rely on keyword-matched archetype injection. For this
    // commit the legacy full-prompt path stays authoritative; the helper
    // is exercised by unit tests via splitSystemPrompt().
    std::string prompt;
    if (use_rails_only_) {
        const auto split = splitSystemPrompt(base);
        prompt = split.rails;
        // Lazy archetype injection: scan the user prompt for archetype
        // keywords and append the matching recipe blocks. Conservative —
        // empty user prompt means no archetypes added.
        if (!userPrompt.empty()) {
            std::string lowerPrompt;
            lowerPrompt.reserve(userPrompt.size());
            for (char c : userPrompt)
                lowerPrompt += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto& [kw, recipe] : split.archetypes) {
                if (lowerPrompt.find(kw) != std::string::npos) {
                    prompt += "\n";
                    prompt += recipe;
                }
            }
        }
    } else {
        prompt = base;
    }

    // Append MIDI CC context so the AI respects the user's current performance state.
    const float cutoff = knob_.midiCutoffNorm();
    const float reso = knob_.midiResonanceNorm();
    if (cutoff < 0.25f)
        prompt += "MIDI context: filter is currently closed (dark sound).\n";
    else if (cutoff > 0.75f)
        prompt += "MIDI context: filter is currently open (bright sound).\n";
    if (reso > 0.5f)
        prompt += "MIDI context: high resonance is active.\n";

    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty())
        return prompt;
    return prompt + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
}

PatchVector PromptHandler::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

std::string PromptHandler::generateRationale(const std::string& prompt, const PatchStruct& patch) const {
    // Phase 21: priority is (1) LLM-authored rationale carried in the patch,
    // (2) the heuristic below. The LLM emits a sensory 1–2 sentence
    // description alongside the patch params (see system-prompt.md §0), which
    // describes the actual layered architecture instead of stitching together
    // canned fragments from osc[0] alone. The heuristic remains as a
    // last-resort for legacy patches and LLM-disabled paths (Gemini-key
    // missing + local llama.cpp unreachable + heuristic-only fallback).
    //
    // Phase 30: the heuristic was rewritten to enumerate ALL audible oscs
    // (not just osc[0]) in sensory-first register. The previous template
    // claimed "I chose a sawtooth oscillator" even when PatchAugmenter
    // had layered two more oscs on top — the result was a rationale that
    // lied about the patch the user heard. New template leads with the
    // sonic result, names the layer-count, and never claims a specific
    // single oscillator type.
    if (patch.rationale[0] != '\0')
        return std::string(patch.rationale);

    static const char* kOscNames[] = {"sine", "triangle", "sawtooth", "square",
                                       "pulse", "wavetable", "FM", "noise"};
    auto oscName = [&](int idx) -> const char* {
        return (idx >= 0 && idx < 8) ? kOscNames[idx] : "sawtooth";
    };

    // Enumerate audible oscs (vol >= 0.15 + enabled) — the §0 rule 12
    // threshold. Build a sensory body description from however many made
    // it through.
    struct AudibleOsc { const char* name; int semi; };
    std::vector<AudibleOsc> audible;
    for (const auto& o : patch.osc) {
        if (o.enabled && o.volume >= 0.15f) {
            audible.push_back({oscName(static_cast<int>(o.type)),
                               static_cast<int>(o.semitone_offset)});
        }
    }

    std::ostringstream oss;

    // Lead with what's audible — the heard result, not the engineering choice.
    if (audible.size() >= 3) {
        oss << "Three voices stacked";
        // Mention octave spread if present (gives the user a sense of width).
        const int spread = audible.back().semi - audible.front().semi;
        if (std::abs(spread) >= 12)
            oss << " across the octaves";
        oss << " — " << audible[0].name << ", " << audible[1].name
            << ", and " << audible[2].name;
    } else if (audible.size() == 2) {
        oss << "Two " << audible[0].name << "-and-" << audible[1].name
            << " voices breathing together";
    } else if (audible.size() == 1) {
        oss << "A single " << audible[0].name << " voice";
    } else {
        oss << "A quiet patch";
    }

    // Filter character — sensory framing, not parameter naming.
    if (patch.filter.cutoff_hz < 500.0f)
        oss << " sitting low under a dark filter";
    else if (patch.filter.cutoff_hz < 1500.0f)
        oss << " warmed through a half-open filter";
    else if (patch.filter.cutoff_hz < 5000.0f)
        oss << " with the filter wide enough to let the upper partials breathe";
    else
        oss << " in front of a wide-open filter";

    if (patch.filter.env_mod < -0.15f)
        oss << ", blooming open on the tail instead of the attack";
    else if (patch.filter.resonance > 0.6f)
        oss << ", resonance pushed for bite";

    // Amplitude envelope shape.
    if (patch.amp_env.attack_s > 1.5f)
        oss << ". The sound swells in slowly";
    else if (patch.amp_env.attack_s > 0.3f)
        oss << ". A gentle attack lets it bloom";
    else if (patch.amp_env.attack_s < 0.01f)
        oss << ". Instant attack — the note lands hard";

    if (patch.amp_env.release_s > 4.0f)
        oss << " and the tail hangs in the air";
    else if (patch.amp_env.release_s > 1.5f)
        oss << " with a long release";

    // Modulation movement — distinguish "two LFOs at different speeds" from
    // "one LFO". Two coprime LFOs is the cinematic "ever-changing" signature.
    bool lfo1Active = patch.lfo[0].depth > 0.2f && patch.lfo[0].target != LfoTarget::None;
    bool lfo2Active = patch.lfo[1].depth > 0.2f && patch.lfo[1].target != LfoTarget::None;
    if (lfo1Active && lfo2Active && patch.lfo[0].target != patch.lfo[1].target) {
        oss << ". Two slow LFOs at different speeds keep the patch ever-changing";
    } else if (lfo1Active) {
        const char* lfoTarget = (patch.lfo[0].target == LfoTarget::Pitch)          ? "pitch drift"
                                : (patch.lfo[0].target == LfoTarget::FilterCutoff) ? "filter breath"
                                : (patch.lfo[0].target == LfoTarget::Amplitude)    ? "tremolo"
                                : (patch.lfo[0].target == LfoTarget::Pan)          ? "auto-pan"
                                                                                   : "modulation";
        oss << ". " << lfoTarget << " adds slow movement";
        // Capitalise sentence start.
        const auto firstChar = oss.tellp() - static_cast<std::streampos>(strlen(lfoTarget) + 16);
        (void)firstChar;
    }

    // Space.
    if (patch.reverb.mix > 0.4f)
        oss << ". Cathedral reverb places it in a wide, ambient space";
    else if (patch.reverb.mix > 0.15f)
        oss << ". A little reverb adds depth";

    if (patch.delay.mix > 0.2f)
        oss << " with a stereo delay trailing behind";

    // MIDI context — kept because it's a concrete observation, but the
    // empty session-feedback boilerplate ("steered me toward this timbral
    // direction…") is gone. Per Phase 30 Brand Guardian audit: boilerplate
    // that says nothing is worse than silence.
    const float cutoff = knob_.midiCutoffNorm();
    if (cutoff < 0.25f)
        oss << ". I respected your MIDI filter position (held low)";
    else if (cutoff > 0.75f)
        oss << ". I matched your MIDI filter position (held high)";

    oss << ".";
    (void)prompt; // recap suppressed by design; argument retained for ABI/test compat.
    return oss.str();
}

} // namespace agentic_synth::agent
