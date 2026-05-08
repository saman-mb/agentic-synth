#include "mapper/HeuristicParser.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace agentsynth {
namespace {

struct Rule {
    const char* keyword;
    std::function<void(PatchStruct&)> apply;
};

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// Whole-word match: keyword must be bordered by non-alpha chars or string edges.
bool has_word(const std::string& text, const char* word) {
    const std::string w(word);
    std::size_t pos = text.find(w);
    while (pos != std::string::npos) {
        bool left_ok = (pos == 0) || !std::isalpha(static_cast<unsigned char>(text[pos - 1]));
        bool right_ok =
            (pos + w.size() == text.size()) || !std::isalpha(static_cast<unsigned char>(text[pos + w.size()]));
        if (left_ok && right_ok)
            return true;
        pos = text.find(w, pos + 1);
    }
    return false;
}

// clang-format off
const std::vector<Rule>& get_rules() {
    static const std::vector<Rule> rules = {
        // ── Brightness / Tone ───────────────────────────────────────────────
        {"dark",   [](PatchStruct& p) {
            p.filter.cutoff_hz = 400.0f;
            p.filter.drive     = 0.1f;
        }},
        {"bright", [](PatchStruct& p) {
            p.filter.cutoff_hz = 8000.0f;
            p.filter.resonance = 0.15f;
        }},
        {"warm",   [](PatchStruct& p) {
            p.filter.cutoff_hz = 1800.0f;
            p.filter.resonance = 0.1f;
            p.osc[0].type      = OscType::Triangle;
        }},
        {"cold",   [](PatchStruct& p) {
            p.filter.cutoff_hz = 5000.0f;
            p.filter.resonance = 0.7f;
            p.osc[0].type      = OscType::Square;
        }},
        {"icy",    [](PatchStruct& p) {
            p.filter.cutoff_hz = 6000.0f;
            p.filter.resonance = 0.75f;
        }},
        {"mellow", [](PatchStruct& p) {
            p.filter.cutoff_hz = 1200.0f;
            p.filter.resonance = 0.05f;
            p.amp_env.attack_s = 0.05f;
        }},
        {"harsh",  [](PatchStruct& p) {
            p.filter.cutoff_hz = 6500.0f;
            p.filter.resonance = 0.8f;
            p.filter.drive     = 0.5f;
        }},

        // ── Space / Reverb ──────────────────────────────────────────────────
        {"wide",      [](PatchStruct& p) {
            p.reverb.width = 1.0f;
            p.reverb.mix   = 0.4f;
        }},
        {"narrow",    [](PatchStruct& p) {
            p.reverb.width = 0.0f;
            p.reverb.mix   = 0.0f;
        }},
        {"spacious",  [](PatchStruct& p) {
            p.reverb.size = 0.85f;
            p.reverb.mix  = 0.5f;
        }},
        {"ambient",   [](PatchStruct& p) {
            p.reverb.size      = 0.9f;
            p.reverb.mix       = 0.6f;
            p.amp_env.attack_s = 0.3f;
        }},
        {"dry",       [](PatchStruct& p) {
            p.reverb.mix = 0.0f;
            p.delay.mix  = 0.0f;
        }},
        {"wet",       [](PatchStruct& p) {
            p.reverb.mix = 0.5f;
            p.delay.mix  = 0.3f;
        }},
        {"room",      [](PatchStruct& p) {
            p.reverb.size = 0.4f;
            p.reverb.mix  = 0.3f;
        }},
        {"hall",      [](PatchStruct& p) {
            p.reverb.size = 0.8f;
            p.reverb.mix  = 0.45f;
        }},
        {"reverb",    [](PatchStruct& p) {
            p.reverb.mix = 0.4f;
        }},
        {"cathedral", [](PatchStruct& p) {
            p.reverb.size    = 1.0f;
            p.reverb.mix     = 0.7f;
            p.reverb.damping = 0.2f;
        }},
        {"underwater",[](PatchStruct& p) {
            p.filter.cutoff_hz = 300.0f;
            p.filter.resonance = 0.5f;
            p.reverb.mix       = 0.6f;
        }},

        // ── Modulation / Motion ─────────────────────────────────────────────
        {"evolving",  [](PatchStruct& p) {
            p.lfo[0].rate_hz = 0.1f;
            p.lfo[0].depth   = 0.7f;
            p.lfo[0].target  = LfoTarget::Filter;
        }},
        {"morphing",  [](PatchStruct& p) {
            p.lfo[0].rate_hz = 0.05f;
            p.lfo[0].depth   = 0.9f;
            p.lfo[0].target  = LfoTarget::Filter;
        }},
        {"static",    [](PatchStruct& p) {
            p.lfo[0].depth = 0.0f;
            p.lfo[1].depth = 0.0f;
        }},
        {"tremolo",   [](PatchStruct& p) {
            p.lfo[0].target  = LfoTarget::Volume;
            p.lfo[0].rate_hz = 5.0f;
            p.lfo[0].depth   = 0.5f;
        }},
        {"vibrato",   [](PatchStruct& p) {
            p.lfo[0].target  = LfoTarget::Pitch;
            p.lfo[0].rate_hz = 5.5f;
            p.lfo[0].depth   = 0.3f;
        }},
        {"wobble",    [](PatchStruct& p) {
            p.lfo[0].target  = LfoTarget::Filter;
            p.lfo[0].rate_hz = 3.0f;
            p.lfo[0].depth   = 0.8f;
        }},
        {"glitchy",   [](PatchStruct& p) {
            p.lfo[0].rate_hz  = 15.0f;
            p.lfo[0].depth    = 0.9f;
            p.lfo[0].waveform = LfoWaveform::Square;
        }},

        // ── Envelope / Attack Character ─────────────────────────────────────
        {"plucky",    [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.001f;
            p.amp_env.decay_s   = 0.08f;
            p.amp_env.sustain   = 0.0f;
            p.amp_env.release_s = 0.1f;
        }},
        {"pad",       [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.5f;
            p.amp_env.decay_s   = 0.5f;
            p.amp_env.sustain   = 0.8f;
            p.amp_env.release_s = 1.5f;
            p.reverb.mix        = 0.35f;
        }},
        {"punchy",    [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.001f;
            p.amp_env.decay_s   = 0.05f;
            p.amp_env.sustain   = 0.3f;
            p.amp_env.release_s = 0.1f;
        }},
        {"soft",      [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.15f;
            p.amp_env.decay_s   = 0.2f;
            p.amp_env.sustain   = 0.7f;
            p.amp_env.release_s = 0.4f;
        }},
        {"sharp",     [](PatchStruct& p) {
            p.amp_env.attack_s = 0.001f;
            p.amp_env.decay_s  = 0.03f;
            p.amp_env.sustain  = 0.0f;
        }},
        {"slow",      [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.8f;
            p.amp_env.release_s = 1.0f;
        }},
        {"fast",      [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.001f;
            p.amp_env.release_s = 0.05f;
        }},
        {"legato",    [](PatchStruct& p) {
            p.portamento_s     = 0.12f;
            p.amp_env.attack_s = 0.05f;
        }},
        {"staccato",  [](PatchStruct& p) {
            p.amp_env.sustain   = 0.2f;
            p.amp_env.release_s = 0.03f;
        }},

        // ── Oscillator Waveform ─────────────────────────────────────────────
        {"sine",      [](PatchStruct& p) { p.osc[0].type = OscType::Sine; }},
        {"pure",      [](PatchStruct& p) {
            p.osc[0].type      = OscType::Sine;
            p.filter.resonance = 0.05f;
        }},
        {"saw",       [](PatchStruct& p) { p.osc[0].type = OscType::Saw; }},
        {"sawtooth",  [](PatchStruct& p) { p.osc[0].type = OscType::Saw; }},
        {"square",    [](PatchStruct& p) {
            p.osc[0].type        = OscType::Square;
            p.osc[0].pulse_width = 0.5f;
        }},
        {"hollow",    [](PatchStruct& p) {
            p.osc[0].type        = OscType::Square;
            p.osc[0].pulse_width = 0.25f;
        }},
        {"triangle",  [](PatchStruct& p) { p.osc[0].type = OscType::Triangle; }},
        {"noise",     [](PatchStruct& p) {
            p.osc[0].type      = OscType::Noise;
            p.filter.resonance = 0.6f;
        }},

        // ── Register / Frequency Range ──────────────────────────────────────
        {"bass",      [](PatchStruct& p) {
            p.osc[0].semitone_offset = -12.0f;
            p.filter.cutoff_hz       = 350.0f;
            p.master_gain            = 0.85f;
        }},
        {"sub",       [](PatchStruct& p) {
            p.osc[0].semitone_offset = -24.0f;
            p.osc[0].type            = OscType::Sine;
        }},
        {"lead",      [](PatchStruct& p) {
            p.osc[0].type      = OscType::Saw;
            p.filter.cutoff_hz = 4500.0f;
            p.filter.resonance = 0.35f;
        }},
        {"high",      [](PatchStruct& p) {
            p.osc[0].semitone_offset = 12.0f;
            p.filter.cutoff_hz       = 10000.0f;
        }},
        {"treble",    [](PatchStruct& p) {
            p.osc[0].semitone_offset = 12.0f;
            p.filter.cutoff_hz       = 9000.0f;
        }},
        {"low",       [](PatchStruct& p) {
            p.osc[0].semitone_offset = -12.0f;
            p.filter.cutoff_hz       = 300.0f;
        }},
        {"mid",       [](PatchStruct& p) { p.filter.cutoff_hz = 1000.0f; }},

        // ── Effects ─────────────────────────────────────────────────────────
        {"echo",      [](PatchStruct& p) {
            p.delay.mix      = 0.35f;
            p.delay.time_s   = 0.375f;
            p.delay.feedback = 0.45f;
        }},
        {"delay",     [](PatchStruct& p) {
            p.delay.mix    = 0.25f;
            p.delay.time_s = 0.5f;
        }},
        {"distorted", [](PatchStruct& p) {
            p.filter.drive     = 0.8f;
            p.filter.resonance = 0.5f;
        }},
        {"fuzzy",     [](PatchStruct& p) { p.filter.drive = 0.75f; }},
        {"clean",     [](PatchStruct& p) {
            p.filter.drive     = 0.0f;
            p.filter.resonance = 0.1f;
            p.reverb.mix       = 0.05f;
        }},
        {"metallic",  [](PatchStruct& p) {
            p.osc[0].fm_depth  = 0.7f;
            p.osc[0].fm_ratio  = 3.5f;
            p.filter.resonance = 0.4f;
        }},

        // ── Polyphony / Voice ────────────────────────────────────────────────
        {"unison",    [](PatchStruct& p) {
            p.osc[1].enabled      = 1;
            p.osc[1].detune_cents = 12.0f;
            p.osc[1].volume       = 0.7f;
            p.osc[1].fm_ratio     = 1.0f;
            p.osc[1].pulse_width  = 0.5f;
        }},
        {"mono",      [](PatchStruct& p) {
            p.voice_count  = 1;
            p.portamento_s = 0.08f;
        }},
        {"poly",      [](PatchStruct& p) { p.voice_count = 8; }},

        // ── Mood / Character ─────────────────────────────────────────────────
        {"aggressive",[](PatchStruct& p) {
            p.filter.resonance = 0.7f;
            p.filter.drive     = 0.6f;
            p.osc[0].type      = OscType::Saw;
            p.filter.cutoff_hz = 5000.0f;
        }},
        {"peaceful",  [](PatchStruct& p) {
            p.filter.cutoff_hz = 1500.0f;
            p.reverb.mix       = 0.4f;
            p.osc[0].type      = OscType::Sine;
            p.amp_env.attack_s = 0.15f;
        }},
        {"mysterious",[](PatchStruct& p) {
            p.filter.cutoff_hz = 600.0f;
            p.reverb.mix       = 0.5f;
            p.lfo[0].rate_hz   = 0.08f;
            p.lfo[0].depth     = 0.5f;
        }},
        {"organic",   [](PatchStruct& p) {
            p.amp_env.attack_s = 0.015f;
            p.filter.resonance = 0.08f;
            p.osc[0].type      = OscType::Triangle;
        }},
        {"digital",   [](PatchStruct& p) {
            p.osc[0].type      = OscType::Square;
            p.filter.resonance = 0.65f;
            p.filter.drive     = 0.3f;
        }},
        {"pluck",     [](PatchStruct& p) {
            p.amp_env.attack_s  = 0.001f;
            p.amp_env.decay_s   = 0.1f;
            p.amp_env.sustain   = 0.0f;
            p.amp_env.release_s = 0.15f;
        }},
    };
    return rules;
}
// clang-format on

} // namespace

PatchStruct HeuristicParser::parse(const std::string& prompt) const {
    PatchStruct patch = make_default_patch();
    const std::string lower = to_lower(prompt);
    for (const auto& rule : get_rules()) {
        if (has_word(lower, rule.keyword)) {
            rule.apply(patch);
        }
    }
    return patch;
}

} // namespace agentsynth
