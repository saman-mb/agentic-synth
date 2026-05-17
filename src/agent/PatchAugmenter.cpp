#include "agent/PatchAugmenter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

namespace agentic_synth::agent {

namespace {

// Append one user-facing action string to PatchStruct::augmenter_actions.
// Strings are pipe-separated and the buffer is always null-terminated so the
// UI can split on '|' without bounds drama. Silently truncates if the buffer
// is too full — the goal is to give the user *some* signal, not a
// guaranteed-complete log.
//
// Phase 30: also clears patch.rationale[]. Any augmenter mutation that
// produces an action string by definition changes the topology the LLM had
// in mind when it wrote its rationale, so the LLM's prose is now stale and
// would lie ("I chose a sawtooth oscillator" while the patch is now 3-osc
// cinematic). Clearing the buffer makes PromptHandler::generateRationale
// fall through to the post-augment heuristic, which reads what's actually
// audible. The audit log in augmenter_actions retains the user-facing trail.
void appendAction(PatchStruct& p, const char* action) noexcept {
    if (!action || !*action) return;
    const std::size_t cap = sizeof(p.augmenter_actions);
    const std::size_t used = std::strlen(p.augmenter_actions);
    const std::size_t need = std::strlen(action) + (used > 0 ? 1 : 0);
    if (used + need + 1 > cap) return;
    if (used > 0) {
        p.augmenter_actions[used] = '|';
        std::strncpy(p.augmenter_actions + used + 1, action, cap - used - 2);
    } else {
        std::strncpy(p.augmenter_actions, action, cap - 1);
    }
    p.augmenter_actions[cap - 1] = '\0';

    // Invalidate stale LLM rationale — heuristic will regenerate truthfully.
    p.rationale[0] = '\0';
}

// Lower-ASCII view of `s`. Cold-path only (one call per LLM submission), so
// the per-call allocation is acceptable. Mirrors PromptHandler.cpp's helper —
// kept local rather than exported so PatchAugmenter has no header dependency
// on PromptHandler.
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

// Word-boundary-aware substring match. Multi-token needles (any with a space)
// skip the boundary check on the side adjacent to the space because they
// already encode their own boundary. Same semantics as the matching helper in
// PromptHandler.cpp's isRelativePrompt — intentionally duplicated rather than
// exported so PatchAugmenter can be unit-tested in isolation.
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

// Keywords that signal the producer explicitly wants a minimal / single-osc
// patch. When any match hits, augmentPatch returns without layering. The list
// is drawn from §0 rule 12's stated exemptions ("pure sine sub",
// "single-osc lead") plus common everyday phrasings producers use to ask for
// minimalism.
constexpr std::array<std::string_view, 16> kSimpleKeywords = {
    "pure",
    "simple",
    "minimal",
    "single",
    "just a",
    "raw",
    "clean sub",
    "pure sine",
    "pure tone",
    "one osc",
    "alone",
    " solo",
    "single oscillator",
    "sine sub",
    "single-osc",
    "one oscillator",
};

// Audibility threshold from §0 rule 12 — "each enabled osc must have
// volume >= 0.15". Below this the LLM technically marked the osc enabled but
// the producer cannot hear it, so we count it as silent for layering
// decisions.
constexpr float kAudibilityThreshold = 0.15f;

int countAudibleOscs(const PatchStruct& p) noexcept {
    int n = 0;
    for (const auto& o : p.osc)
        if (o.enabled && o.volume >= kAudibilityThreshold)
            ++n;
    return n;
}

int firstAudibleOscIndex(const PatchStruct& p) noexcept {
    for (int i = 0; i < kMaxOscillators; ++i)
        if (p.osc[i].enabled && p.osc[i].volume >= kAudibilityThreshold)
            return i;
    return -1;
}

bool isNoiseOnly(const PatchStruct& p) noexcept {
    int audible = 0;
    int noiseAudible = 0;
    for (const auto& o : p.osc) {
        if (o.enabled && o.volume >= kAudibilityThreshold) {
            ++audible;
            if (o.type == OscType::Noise)
                ++noiseAudible;
        }
    }
    return audible > 0 && audible == noiseAudible;
}

// Phase 30 — detect cinematic / Kubrick / spooky / drone / ambient pad
// prompts. These should NOT route to Reese (bass topology) when the LLM
// ships a single Saw — they need an octave-spread layered pad with two
// LFOs at coprime rates on different targets. The Reese recipe is wrong
// for these prompts even though it produces 3 oscs.
bool containsCinematicIntent(const std::string& lowerPrompt) noexcept {
    static constexpr std::array<std::string_view, 18> kCinematicTokens{
        "cinematic", "kubrick", "spooky", "ever-changing", "ever changing",
        "evolving", "drone", "ominous pad", "horror", "dread", "foreboding",
        "eldritch", "ambient pad", "ethereal", "atmospheric", "ambient",
        "soundscape", "vangelis",
    };
    for (auto kw : kCinematicTokens) {
        if (containsWord(lowerPrompt, kw))
            return true;
    }
    return false;
}

// Phase 27 — detect when the user named FM-style synthesis explicitly. When
// these tokens are present, osc[0].type MUST be FM. The enhancer-prompt + §5
// anti-pattern tell the LLM the same rule, but the augmenter is the
// runtime floor: even if both LLMs whiff, we coerce the type here so the
// shipped patch matches the named synthesis technique.
bool containsFmIntent(const std::string& lowerPrompt) noexcept {
    static constexpr std::array<std::string_view, 16> kFmTokens{
        "fm", "fm-style", "fm style", "dx", "dx7", "dx-style", "yamaha",
        "operator", "fm8", "tine", "rhodes", "electric piano",
        "bell", "marimba", "vibraphone", "ring mod",
    };
    for (auto kw : kFmTokens) {
        if (containsWord(lowerPrompt, kw))
            return true;
    }
    return false;
}

// Map a prompt's genre cue to the most musical pitched-osc replacement for a
// noise-only patch. Matches the §1.1 oscillator-to-archetype mapping in
// system-prompt.md: bass / lead → saw (toothy, harmonic-rich), pad / ambient
// → triangle or sine (hollow, weightless), default triangle for textures
// that aren't otherwise tagged. FM intent takes priority over everything.
OscType pickPitchedTypeForNoiseFix(const std::string& lowerPrompt) noexcept {
    if (containsFmIntent(lowerPrompt))
        return OscType::FM;
    if (containsWord(lowerPrompt, "bass"))
        return OscType::Sawtooth;
    if (containsWord(lowerPrompt, "lead"))
        return OscType::Sawtooth;
    if (containsWord(lowerPrompt, "ambient"))
        return OscType::Sine;
    if (containsWord(lowerPrompt, "pad"))
        return OscType::Triangle;
    return OscType::Triangle;
}

// Initialise a fresh OscParams record to neutral defaults. Mirrors what
// make_default_patch() does per osc so the engine never sees an uninitialised
// pulse_width / fm_ratio / pan after augmentation.
void resetOsc(OscParams& o) noexcept {
    o.type = OscType::Sine;
    o.semitone_offset = 0.0f;
    o.detune_cents = 0.0f;
    o.wavetable_pos = 0.0f;
    o.fm_ratio = 1.0f;
    o.fm_depth = 0.0f;
    o.volume = 0.0f;
    o.pan = 0.0f;
    o.pulse_width = 0.5f;
    o.enabled = 0;
}

// Phase 30 — minimum cutoff guard for layered patches.
//
// When the LLM picks a closed filter for "dark / sub / heavy", then the
// augmenter layers detuned partners on top, the partners are filtered to
// inaudibility — the user hears osc[0]'s fundamental + a tiny bit of beating
// and reports "single oscillator with vibrato". Every saw/pad layering path
// must raise the cutoff into a band where the harmonic content of the
// added partners actually survives. Reese / Pad need 1000-1500 Hz minimum;
// bass-coded patches keep their darker cutoff if the LLM picked one that
// already exceeds the floor.
void enforceLayeringCutoff(PatchStruct& p, float floorHz) noexcept {
    if (p.filter.cutoff_hz < floorHz)
        p.filter.cutoff_hz = floorHz;
    // High resonance on a wider-open cutoff sounds piercing; cap it so the
    // patch stays musical after we open the filter.
    if (p.filter.resonance > 0.6f)
        p.filter.resonance = 0.6f;
}

// Auto-layer strategy for a single-saw mainosc. Builds the brand's preferred
// Reese topology (system-prompt.md §1.1, §1.4): detuned saw pair across
// ±10¢ for the chorus beating, sine sub an octave down for low-end weight.
// We preserve osc[0]'s author-set semitone_offset / pan / pulse_width so the
// LLM's other tuning choices survive.
void applyReeseLayering(PatchStruct& p) noexcept {
    const auto& mainOsc = p.osc[0];

    // osc[1] — detune partner. Same waveform, opposite detune sign, slightly
    // lower volume so the result reads as one slightly-thicker voice rather
    // than two voices in unison.
    auto& detune = p.osc[1];
    resetOsc(detune);
    detune.type = mainOsc.type;
    detune.semitone_offset = mainOsc.semitone_offset;
    detune.detune_cents = -10.0f;
    detune.pulse_width = mainOsc.pulse_width;
    detune.volume = 0.7f;
    detune.enabled = 1;

    // osc[2] — sub-octave sine for harmonic anchor. Half volume so it sits
    // under the saws instead of doubling them.
    auto& sub = p.osc[2];
    resetOsc(sub);
    sub.type = OscType::Sine;
    sub.semitone_offset = mainOsc.semitone_offset - 12.0f;
    sub.volume = 0.5f;
    sub.enabled = 1;

    // Reese is bass-y by default, but the detune partner's harmonics still
    // need room above ~800 Hz to read as "chorus" rather than "muffled".
    enforceLayeringCutoff(p, 800.0f);
}

// Phase 30 — cinematic / Kubrick / spooky / drone pad topology.
// Phase 32 DSP refresh — corrected against Reverb Machine Vangelis breakdown,
// MusicRadar cinematic tips, and SonicScoop pads guide.
//
// Distinct from Reese (parallel detuned saws at unison, bass-coded) and
// Pad (detuned triangles + octave shimmer). Cinematic uses:
//   • octave-spread oscillators (osc[0] −12 panned, osc[1] unison panned,
//     osc[2] inharmonic FM at -12 for unsettling buzz — NOT a plain sine sub)
//   • asymmetric detune (-11c / +13c) so beating never centers
//   • hard stereo via panned detuned pair (osc[0] -0.6, osc[1] +0.6)
//   • POSITIVE filter env_mod (bloom OPENS on attack — the cinematic reveal)
//   • slow filter-env attack (~1.8s) so the bloom is felt, not snapped
//   • long amp + filter envelopes (A≥2s, R≥6s)
//   • pre-filter drive (0.35) for the Vangelis CS-80 harmonic glue
//   • LFO1 at ~0.06 Hz on filter cutoff (breathing) — OR WavetablePos when
//     osc[1] is wavetable (spectral motion beats filter sweep there)
//   • LFO2 at ~0.10 Hz on pitch micro-drift (Kubrick monolith move)
//   • cathedral reverb size 0.92, mix CAPPED at 0.45 (above that is mud)
//
// The two LFOs are coprime so the patch never repeats — that's what makes
// "ever-changing" actually change.
void applyCinematicPadLayering(PatchStruct& p) noexcept {
    const auto& mainOsc = p.osc[0];
    const bool partnerIsWavetable = mainOsc.type == OscType::Wavetable;

    // osc[0] — keep the LLM's choice but seat it an octave lower and pan
    // slightly left. Asymmetric -11c detune.
    auto& body = p.osc[0];
    body.semitone_offset = mainOsc.semitone_offset - 12.0f;
    body.detune_cents = -11.0f;
    body.volume = 0.75f;
    body.pan = -0.6f;
    body.enabled = 1;

    // osc[1] — partner at +13c detune (asymmetric vs osc[0]'s -11c so the
    // beating never centers), opposite pan, slightly quieter.
    auto& partner = p.osc[1];
    resetOsc(partner);
    partner.type = partnerIsWavetable ? OscType::Wavetable : OscType::Sawtooth;
    partner.semitone_offset = mainOsc.semitone_offset;
    partner.detune_cents = 13.0f;
    partner.wavetable_pos = 0.30f;
    partner.volume = 0.70f;
    partner.pan = 0.6f;
    partner.enabled = 1;

    // osc[2] — INHARMONIC FM at -12 (NOT plain sine sub). Non-integer
    // modulator ratio at low index creates non-octave partials that buzz
    // unsettlingly — the Kubrick / Vangelis monolith move.
    auto& sub = p.osc[2];
    resetOsc(sub);
    sub.type = OscType::FM;
    sub.semitone_offset = -12.0f; // audible, not -24 sub-cellar
    sub.fm_ratio = 2.73f;          // inharmonic
    sub.fm_depth = 0.35f;          // low index — buzz, not bell
    sub.volume = 0.40f;
    sub.pan = 0.0f;
    sub.enabled = 1;

    // Filter — moderate cutoff with POSITIVE env_mod (bloom OPENS on attack
    // — the cinematic reveal). Drive bumped to 0.35 for Vangelis pre-filter
    // saturation glue.
    p.filter.type = FilterType::LowPass;
    p.filter.cutoff_hz = 1400.0f;
    p.filter.resonance = 0.35f;
    p.filter.env_mod = 0.40f; // POSITIVE: opens on attack, sustains brightness
    p.filter.drive = std::max(p.filter.drive, 0.35f);

    // Envelopes — long swell, long tail. Filter-env attack 1.8s so the
    // bloom is felt as a slow reveal, then decay sustains the brightness.
    p.amp_env.attack_s = std::max(p.amp_env.attack_s, 2.2f);
    p.amp_env.decay_s = std::max(p.amp_env.decay_s, 1.5f);
    p.amp_env.sustain = std::max(p.amp_env.sustain, 0.85f);
    p.amp_env.release_s = std::max(p.amp_env.release_s, 6.0f);
    p.filter_env.attack_s = std::max(p.filter_env.attack_s, 1.8f);
    p.filter_env.decay_s = std::max(p.filter_env.decay_s, 5.0f);
    p.filter_env.sustain = std::max(p.filter_env.sustain, 0.55f);
    p.filter_env.release_s = std::max(p.filter_env.release_s, 5.0f);

    // Two LFOs at coprime rates on different targets. Without this the
    // patch "loops" audibly within seconds. When osc[1] is wavetable,
    // LFO1 targets WavetablePos (more spectral motion than filter sweep).
    auto& lfo1 = p.lfo[0];
    lfo1.waveform = LfoWaveform::Sine;
    lfo1.target = partnerIsWavetable ? LfoTarget::WavetablePos : LfoTarget::FilterCutoff;
    lfo1.rate_hz = 0.06f;
    lfo1.depth = 0.55f;
    lfo1.bpm_sync = 0;

    auto& lfo2 = p.lfo[1];
    lfo2.waveform = LfoWaveform::Triangle;
    lfo2.target = LfoTarget::Pitch;
    lfo2.rate_hz = 0.10f;
    lfo2.depth = 0.04f; // micro-detune drift, not vibrato
    lfo2.bpm_sync = 0;

    // Cathedral reverb — large room (size 0.92) but mix CAPPED at 0.45.
    // Above 0.45 at this size produces mud, not space.
    p.reverb.size = std::max(p.reverb.size, 0.92f);
    p.reverb.damping = 0.55f;
    p.reverb.width = 1.0f;
    p.reverb.mix = std::clamp(p.reverb.mix, 0.30f, 0.45f);

    // Phase E (#265) — pre-filter chorus + tube saturation + reverb-send
    // HPF. Panel quote: "80% of Vangelis lushness lives in chorus + tube
    // saturation pre-filter, NOT in reverb." Defaults match the Dimension-D
    // / Juno-ensemble reference values cited in §32 panel research.
    p.tubesat.drive = std::max(p.tubesat.drive, 0.30f); // harmonic glue
    p.tubesat.mix = 1.0f;
    p.chorus.rate_hz = 0.4f;
    p.chorus.depth = 0.35f;
    p.chorus.mix = std::max(p.chorus.mix, 0.40f); // Juno ensemble
    // HPF the reverb send at 100 Hz so cathedral tail loses sub energy
    // without thinning the dry path. 0 → bypass; we set 100 unconditionally
    // here because cinematic patches always have an active reverb send.
    p.reverb_send_hpf_hz = 100.0f;
}

// Auto-layer strategy for a single-triangle mainosc (pad archetype). Detuned
// triangle partner + octave-up sine for shimmer per system-prompt §1.4 lush-
// pad recipe.
void applyPadLayering(PatchStruct& p) noexcept {
    const auto& mainOsc = p.osc[0];

    auto& detune = p.osc[1];
    resetOsc(detune);
    detune.type = OscType::Triangle;
    detune.semitone_offset = mainOsc.semitone_offset;
    detune.detune_cents = 9.0f;
    detune.volume = 0.7f;
    detune.enabled = 1;

    auto& shimmer = p.osc[2];
    resetOsc(shimmer);
    shimmer.type = OscType::Sine;
    shimmer.semitone_offset = mainOsc.semitone_offset + 12.0f;
    shimmer.volume = 0.4f;
    shimmer.enabled = 1;

    // Pad needs the upper partials to read as "air" — push the floor higher
    // than Reese so the +12 shimmer actually shimmers.
    enforceLayeringCutoff(p, 1500.0f);
}

// Auto-layer strategy for a single-FM mainosc (bell / e-piano archetype).
// Add a clean sine fundamental under the inharmonic tine so the patch has a
// pitched anchor, plus an octave-up sine for shimmer.
void applyFmLayering(PatchStruct& p) noexcept {
    const auto& mainOsc = p.osc[0];

    auto& fundamental = p.osc[1];
    resetOsc(fundamental);
    fundamental.type = OscType::Sine;
    fundamental.semitone_offset = mainOsc.semitone_offset;
    fundamental.volume = 0.5f;
    fundamental.enabled = 1;

    auto& shimmer = p.osc[2];
    resetOsc(shimmer);
    shimmer.type = OscType::Sine;
    shimmer.semitone_offset = mainOsc.semitone_offset + 12.0f;
    shimmer.volume = 0.3f;
    shimmer.enabled = 1;
}

// Generic detune+sub layering for the remaining osc families (Square, Pulse,
// Sine, Wavetable). Saw-style topology adapted to whatever the LLM picked —
// detune partner of the same type, sub-octave sine for harmonic anchor.
void applyGenericLayering(PatchStruct& p) noexcept {
    const auto& mainOsc = p.osc[0];

    auto& detune = p.osc[1];
    resetOsc(detune);
    detune.type = mainOsc.type;
    detune.semitone_offset = mainOsc.semitone_offset;
    detune.detune_cents = mainOsc.type == OscType::Sine ? 4.0f : -10.0f;
    detune.pulse_width = mainOsc.pulse_width;
    detune.volume = 0.6f;
    detune.enabled = 1;

    auto& sub = p.osc[2];
    resetOsc(sub);
    sub.type = OscType::Sine;
    sub.semitone_offset = mainOsc.semitone_offset - 12.0f;
    sub.volume = 0.45f;
    sub.enabled = 1;

    // Same reasoning as Reese — the layered partners need cutoff headroom.
    enforceLayeringCutoff(p, 1000.0f);
}

// Add ONE complementary osc to a 2-osc patch — preserves the LLM's existing
// topology and only fills the empty slot. Sub-octave sine is the safest
// universally-musical addition; "thickener" approach from §1.4. We scan for
// the first non-audible slot and write there; if all three are audible we
// return without touching anything (the caller's count check should prevent
// that anyway).
void addThirdLayer(PatchStruct& p) noexcept {
    int audibleIdx = firstAudibleOscIndex(p);
    if (audibleIdx < 0)
        return;
    const float ref_semitone = p.osc[audibleIdx].semitone_offset;
    for (int i = 0; i < kMaxOscillators; ++i) {
        auto& o = p.osc[i];
        if (o.enabled && o.volume >= kAudibilityThreshold)
            continue;
        resetOsc(o);
        o.type = OscType::Sine;
        o.semitone_offset = ref_semitone - 12.0f;
        o.volume = 0.45f;
        o.enabled = 1;
        return;
    }
}

} // namespace

bool isSimplePrompt(const std::string& prompt) noexcept {
    if (prompt.empty())
        return false;
    const std::string lower = toLowerAscii(prompt);
    for (const auto& kw : kSimpleKeywords) {
        if (containsWord(lower, kw))
            return true;
    }
    return false;
}

bool augmentPatch(PatchStruct& p, const std::string& prompt) noexcept {
    // Short-circuit when the producer asked for a minimal patch. The system
    // prompt allows single-osc patches under this exemption (§0 rule 12); we
    // must honour the explicit ask even if the LLM happens to have shipped a
    // single osc.
    if (isSimplePrompt(prompt))
        return false;

    const std::string lower = toLowerAscii(prompt);
    bool modified = false;

    // --- FM-intent fix (priority 0). When the producer explicitly named the
    // synthesis technique (FM / DX / Rhodes / bell / tine / etc.) but the LLM
    // shipped a non-FM oscillator, we rebuild the patch around the canonical
    // FM topology described in §5 anti-patterns. This catches the case where
    // both the enhancer dropped the "FM" token AND the generator picked
    // Saw/Triangle/Wavetable instead. The classic mistake of closing the
    // filter on top of FM is also corrected here: FM provides the timbre,
    // the filter stays open.
    if (containsFmIntent(lower) && p.osc[0].type != OscType::FM) {
        auto& main = p.osc[0];
        resetOsc(main);
        main.type = OscType::FM;
        // Ratio chosen by sub-intent: "bell" / "glass" → 3.14 (inharmonic
        // bell); "tine" / "rhodes" / "electric piano" → 14.0 (classic DX
        // tine); default 2.01 (glassy carrier). Modulation index = depth.
        if (containsWord(lower, "tine") || containsWord(lower, "rhodes")
            || containsWord(lower, "electric piano") || containsWord(lower, "ep")) {
            main.fm_ratio = 14.0f;
            main.fm_depth = 0.55f;
        } else if (containsWord(lower, "bell") || containsWord(lower, "glass")
                   || containsWord(lower, "chime")) {
            main.fm_ratio = 3.14f;
            main.fm_depth = 0.45f;
        } else {
            main.fm_ratio = 2.01f;
            main.fm_depth = 0.40f;
        }
        main.volume = 0.85f;
        main.enabled = 1;

        // Clean sine fundamental for body weight.
        auto& body = p.osc[1];
        resetOsc(body);
        body.type = OscType::Sine;
        body.volume = 0.55f;
        body.enabled = 1;

        // Octave-up sine for "airy" shimmer.
        auto& shimmer = p.osc[2];
        resetOsc(shimmer);
        shimmer.type = OscType::Sine;
        shimmer.semitone_offset = 12.0f;
        shimmer.volume = 0.20f;
        shimmer.enabled = 1;

        // FM is the timbre — keep the filter wide open. Closed LP on top of
        // FM removes the very partials the modulator generated.
        p.filter.type = FilterType::LowPass;
        p.filter.cutoff_hz = std::max(p.filter.cutoff_hz, 14000.0f);
        p.filter.resonance = std::min(p.filter.resonance, 0.2f);
        p.filter.env_mod = 0.0f;

        std::cerr << "[PatchAugmenter] FM-intent coercion: prompt names FM but LLM "
                     "shipped a non-FM oscillator. Rebuilt patch with FM topology "
                     "(ratio=" << main.fm_ratio << ", depth=" << main.fm_depth
                  << "). prompt='" << prompt << "'\n";
        appendAction(p, "Matched the named synthesis technique (rebuilt around an FM operator + sine body + octave shimmer; filter opened so the FM partials survive)");
        return true;
    }

    // --- Noise-only fix (priority 1). White-noise-only patches are read by
    // the listener as "static, not sound" (§4.2 anti-pattern). We force osc[0]
    // to a pitched fundamental that matches the prompt's genre, demote the
    // noise component to osc[2] as a texture layer, and synthesise a detune
    // partner at osc[1] so the result is still a 3-osc patch.
    if (isNoiseOnly(p)) {
        // Capture the noise volume the LLM originally chose so we keep the
        // texture character it intended (just blended underneath instead of
        // sitting in front). Floor at 0.25 to make sure the layer is
        // audible per §0 rule 12.
        float noiseVol = 0.0f;
        for (const auto& o : p.osc)
            if (o.enabled && o.type == OscType::Noise)
                noiseVol = std::max(noiseVol, o.volume);
        if (noiseVol < 0.25f)
            noiseVol = 0.25f;

        const OscType pitched = pickPitchedTypeForNoiseFix(lower);

        auto& main = p.osc[0];
        resetOsc(main);
        main.type = pitched;
        main.volume = 0.6f;
        main.enabled = 1;

        auto& detune = p.osc[1];
        resetOsc(detune);
        detune.type = pitched == OscType::Sine ? OscType::Sine : pitched;
        detune.detune_cents = pitched == OscType::Sine ? 4.0f : -8.0f;
        detune.volume = 0.4f;
        detune.enabled = 1;

        auto& noise = p.osc[2];
        resetOsc(noise);
        noise.type = OscType::Noise;
        noise.volume = noiseVol;
        noise.enabled = 1;

        std::cerr << "[PatchAugmenter] Auto-fixed noise-only patch (Noise → osc[2] @ "
                  << noiseVol << ", pitched osc[0] = "
                  << (pitched == OscType::Sawtooth ? "Saw"
                       : pitched == OscType::Triangle ? "Triangle"
                       : pitched == OscType::Sine ? "Sine" : "?")
                  << "): prompt='" << prompt << "'\n";
        appendAction(p,
            pitched == OscType::Sawtooth ? "Gave the storm a pitched body (added saw fundamental + detuned partner; noise demoted to texture layer)"
            : pitched == OscType::Triangle ? "Gave the noise a pitched body (added triangle fundamental + detuned partner; noise demoted to texture layer)"
            : "Gave the noise a pitched body (added sine fundamental + 4¢ partner; noise demoted to texture layer)");
        return true;
    }

    // --- Cinematic-pad coercion (priority 1.5). Runs AFTER noise-only fix
    // so a noise-only patch with a cinematic prompt still gets a pitched
    // fundamental first. When the LLM ships a non-FM, non-noise-only patch
    // for a cinematic-keyword prompt, and the result is structurally bass-
    // coded (closed filter < 1000 Hz or fewer than 3 audible oscs), we
    // rebuild around the cinematic-pad recipe (octave-spread, panned
    // detune pair, sub anchor, NEGATIVE filter env_mod, two coprime LFOs,
    // cathedral reverb).
    if (containsCinematicIntent(lower) && !containsFmIntent(lower)) {
        const int audibleCount = countAudibleOscs(p);
        const bool closedFilter = p.filter.cutoff_hz < 1000.0f;
        const bool underLayered = audibleCount < 3;
        if (closedFilter || underLayered) {
            applyCinematicPadLayering(p);
            std::cerr << "[PatchAugmenter] Cinematic-pad coercion: prompt names "
                         "cinematic/spooky/Kubrick/drone but patch was "
                      << (underLayered ? "under-layered" : "bass-coded")
                      << ". Rebuilt with octave-spread + panned detune + sub anchor "
                         "+ coprime LFOs + cathedral reverb. prompt='" << prompt << "'\n";
            appendAction(p, "Reshaped into a cinematic pad (two saws breathing wide with asymmetric detune over an inharmonic FM anchor that buzzes unsettlingly; the filter blooms open on the attack as a slow cinematic reveal; two coprime LFOs keep it ever-changing)");
            return true;
        }
    }

    // --- Under-layered fix (priority 2). The §0 rule 12 contract demands
    // all three oscillators contribute audibly. We graduate the response by
    // count: a fully-layered patch (3 audible) is untouched; a 2-osc patch
    // gets the third slot filled while osc[0]/osc[1] are preserved; a 1-osc
    // patch gets rebuilt with a brand topology around the LLM's seed
    // waveform.
    const int audible = countAudibleOscs(p);
    if (audible >= 3) {
        // Already 3-osc layered — honour what the LLM produced.
        return false;
    }

    if (audible == 2) {
        // Preserve the two voices the LLM authored; just fill the empty
        // slot with a sub-octave sine anchor (§1.4 thickener). The user
        // got a layered patch; we're just adding the missing depth.
        addThirdLayer(p);
        std::cerr << "[PatchAugmenter] Auto-layered patch (2-osc → 3-osc, "
                     "added sub-octave sine in empty slot): prompt='" << prompt << "'\n";
        appendAction(p, "Thickened the low end (filled empty 3rd slot with a sub-octave sine anchor)");
        return true;
    }

    if (audible == 1) {
        const int idx = firstAudibleOscIndex(p);
        // The single-audible-osc case always has the audible osc at
        // p.osc[idx]. Move it to slot 0 if it isn't there already so the
        // layering helpers below can assume osc[0] is the seed.
        if (idx > 0) {
            std::swap(p.osc[0], p.osc[idx]);
        }
        const OscType seed = p.osc[0].type;
        const char* strategy = nullptr;
        switch (seed) {
        case OscType::Sawtooth:
            applyReeseLayering(p);
            strategy = "Reese topology (Saw + detune partner + sub sine)";
            break;
        case OscType::Triangle:
            applyPadLayering(p);
            strategy = "Pad topology (Triangle + detune partner + octave shimmer)";
            break;
        case OscType::FM:
            applyFmLayering(p);
            strategy = "FM topology (FM tine + sine fundamental + octave shimmer)";
            break;
        case OscType::Sine:
        case OscType::Square:
        case OscType::Pulse:
        case OscType::Wavetable:
        case OscType::Noise: // unreachable — handled above, kept for switch completeness
            applyGenericLayering(p);
            strategy = "Generic detune + sub topology";
            break;
        }
        modified = true;
        std::cerr << "[PatchAugmenter] Auto-layered patch (single-osc → "
                  << (strategy ? strategy : "generic") << "): prompt='" << prompt << "'\n";
        const char* userMsg =
            seed == OscType::Sawtooth  ? "Layered the patch into a Reese (detuned saw partner + sub-octave sine for depth)"
          : seed == OscType::Triangle ? "Layered the patch into a pad (detuned triangle partner + octave shimmer for air)"
          : seed == OscType::FM        ? "Layered the FM tine over a clean sine fundamental (plus octave shimmer)"
          :                              "Layered the single oscillator (detune partner + sub-octave anchor)";
        appendAction(p, userMsg);
        return modified;
    }

    if (audible == 0) {
        // Pathological case: LLM emitted a fully silent patch. Force a
        // sensible default rather than dispatching nothing to the engine.
        const OscType pitched = pickPitchedTypeForNoiseFix(lower);
        auto& main = p.osc[0];
        resetOsc(main);
        main.type = pitched;
        main.volume = 0.7f;
        main.enabled = 1;
        applyReeseLayering(p); // re-uses osc[1]/[2] for detune + sub
        std::cerr << "[PatchAugmenter] Auto-recovered silent patch (all oscs muted) "
                     "with Reese topology: prompt='" << prompt << "'\n";
        appendAction(p, "Rescued a silent patch with a default Reese (saw + detuned partner + sub sine)");
        return true;
    }

    return modified;
}

} // namespace agentic_synth::agent
