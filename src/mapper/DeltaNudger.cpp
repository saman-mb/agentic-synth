#include "mapper/DeltaNudger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include "mapper/LlmTelemetry.h"

namespace agentic_synth::mapper {

namespace {

// ── JSON helpers (mirror of GeminiSampler's local helpers) ───────────────────
//
// Kept as a private duplicate rather than promoted to a shared TU because the
// mapper layer deliberately avoids a shared "json utils" header — both
// samplers only need a few escape/extract primitives, and the cost of two
// small copies is lower than the cost of inverting the dependency graph.

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (i + 1 >= s.size()) break;
        const char n = s[++i];
        switch (n) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            if (i + 4 >= s.size()) return out;
            const std::string hex = s.substr(i + 1, 4);
            i += 4;
            try {
                const auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                if (cp < 0x80) out += static_cast<char>(cp);
            } catch (...) {}
            break;
        }
        default: out += n; break;
        }
    }
    return out;
}

// Locate a JSON string-valued field — `key` must include surrounding quotes.
// Returns the unescaped string contents, or empty if not found / not a string.
std::string find_string_field(const std::string& resp, const std::string& key) {
    auto pos = resp.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':')) ++pos;
    if (pos >= resp.size() || resp[pos] != '"') return {};
    ++pos;
    std::string raw;
    while (pos < resp.size()) {
        if (resp[pos] == '\\') {
            if (pos + 1 >= resp.size()) break;
            raw += resp[pos]; raw += resp[pos + 1]; pos += 2; continue;
        }
        if (resp[pos] == '"') break;
        raw += resp[pos++];
    }
    return json_unescape(raw);
}

// Locate a JSON numeric field. Returns NaN when not found / not numeric.
double find_number_field(const std::string& resp, const std::string& key, std::size_t from = 0) {
    auto pos = resp.find(key, from);
    if (pos == std::string::npos) return std::nan("");
    pos += key.size();
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':')) ++pos;
    // Allow optional leading sign.
    const std::size_t start = pos;
    if (pos < resp.size() && (resp[pos] == '-' || resp[pos] == '+')) ++pos;
    bool has_digit = false;
    while (pos < resp.size() && (std::isdigit(static_cast<unsigned char>(resp[pos])) ||
                                 resp[pos] == '.' || resp[pos] == 'e' ||
                                 resp[pos] == 'E' || resp[pos] == '+' || resp[pos] == '-')) {
        if (std::isdigit(static_cast<unsigned char>(resp[pos]))) has_digit = true;
        ++pos;
    }
    if (!has_digit) return std::nan("");
    try {
        return std::stod(resp.substr(start, pos - start));
    } catch (...) { return std::nan(""); }
}

// Tempfile guard — copied from GeminiSampler's local TempFile.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& contents) {
        const char* tmp = std::getenv("TMPDIR");
        if (tmp == nullptr || *tmp == 0) tmp = "/tmp";
        std::mt19937_64 rng{std::random_device{}()};
        const auto rnd = rng();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/dnudge_req_%llx.json", static_cast<unsigned long long>(rnd));
        path = std::string(tmp) + buf;
        std::ofstream f(path, std::ios::binary);
        f << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// Compact JSON brief of a single archetype patch — only the audible/musical
// axes Gemini needs to score the match. Mirrors the §5.3 refinement wrapper's
// philosophy: hand the model the smallest patch surface that conveys
// timbral identity, not the full PatchStruct (which would burn tokens on
// engine-internal padding bytes).
std::string patchBriefJson(const PatchStruct& p) {
    static const char* kOsc[] = {"Sine","Triangle","Sawtooth","Square","Pulse","Wavetable","FM","Noise"};
    static const char* kLfoT[] = {"None","Pitch","FilterCutoff","Amplitude","Pan","WavetablePos","FmRatio"};
    auto oscName = [&](OscType t) { return kOsc[static_cast<int>(t)]; };
    auto lfoTargetName = [&](LfoTarget t) { return kLfoT[static_cast<int>(t)]; };

    std::ostringstream o;
    o << "{\"osc\":[";
    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& osc = p.osc[i];
        if (i > 0) o << ",";
        o << "{\"type\":\"" << oscName(osc.type) << "\","
          << "\"volume\":" << osc.volume << ","
          << "\"enabled\":" << (osc.enabled ? "true" : "false") << "}";
    }
    o << "],\"filter\":{\"cutoff_hz\":" << p.filter.cutoff_hz
      << ",\"resonance\":" << p.filter.resonance
      << ",\"drive\":" << p.filter.drive << "}";
    o << ",\"amp_env\":{\"attack_s\":" << p.amp_env.attack_s
      << ",\"release_s\":" << p.amp_env.release_s << "}";
    o << ",\"reverb\":{\"size\":" << p.reverb.size << ",\"mix\":" << p.reverb.mix << "}";
    o << ",\"chorus\":{\"mix\":" << p.chorus.mix << "}";
    o << ",\"tubesat\":{\"drive\":" << p.tubesat.drive << "}";
    o << ",\"lfo[0]\":{\"target\":\"" << lfoTargetName(p.lfo[0].target)
      << "\",\"depth\":" << p.lfo[0].depth
      << ",\"rate_hz\":" << p.lfo[0].rate_hz << "}";
    o << "}";
    return o.str();
}

// Per-axis percentage cap. Bigger jumps break archetypes; this is the same
// budget the system prompt asks the model for, enforced server-side.
struct AxisCap {
    const char* path;
    float max_pct;
};
constexpr std::array<AxisCap, 7> kAxisCaps{{
    {"filter.cutoff_hz",      30.0f},
    {"amp_env.attack_s",      50.0f},
    {"reverb.mix",            30.0f},
    {"chorus.mix",            50.0f},
    {"tubesat.drive",         50.0f},
    {"lfo[0].depth",          50.0f},
    {"lfo[0].rate_hz",        30.0f},
}};

float lookupAxisCap(const std::string& path) noexcept {
    for (const auto& a : kAxisCaps) if (path == a.path) return a.max_pct;
    // Sensible default for the unlisted-but-whitelisted paths in
    // applyNudges (decay, sustain, release, env.attack_s, etc.).
    return 30.0f;
}

// Apply a single nudge to `p` in place. Returns true when the path was
// recognised (i.e. whitelisted) — caller uses the return value to keep the
// applied-nudge list in sync with what actually mutated the patch.
bool applyOneNudge(PatchStruct& p, const Nudge& n) noexcept {
    const float cap = lookupAxisCap(n.path);
    const float clamped_pct = std::clamp(n.delta_percent, -cap, cap);
    const float k = 1.0f + clamped_pct / 100.0f;

    auto scale = [&](float& field) { field = field * k; };

    if (n.path == "filter.cutoff_hz") { scale(p.filter.cutoff_hz); return true; }
    if (n.path == "filter.resonance") { scale(p.filter.resonance); return true; }
    if (n.path == "filter.drive")     { scale(p.filter.drive); return true; }

    if (n.path == "amp_env.attack_s") { scale(p.amp_env.attack_s); return true; }
    if (n.path == "amp_env.decay_s")  { scale(p.amp_env.decay_s); return true; }
    if (n.path == "amp_env.release_s"){ scale(p.amp_env.release_s); return true; }

    if (n.path == "filter_env.attack_s") { scale(p.filter_env.attack_s); return true; }
    if (n.path == "filter_env.decay_s")  { scale(p.filter_env.decay_s); return true; }

    if (n.path == "reverb.mix")  { scale(p.reverb.mix); return true; }
    if (n.path == "reverb.size") { scale(p.reverb.size); return true; }

    if (n.path == "chorus.mix")     { scale(p.chorus.mix); return true; }
    if (n.path == "chorus.depth")   { scale(p.chorus.depth); return true; }
    if (n.path == "chorus.rate_hz") { scale(p.chorus.rate_hz); return true; }

    if (n.path == "tubesat.drive") { scale(p.tubesat.drive); return true; }
    if (n.path == "tubesat.mix")   { scale(p.tubesat.mix); return true; }

    if (n.path == "lfo[0].depth")   { scale(p.lfo[0].depth); return true; }
    if (n.path == "lfo[0].rate_hz") { scale(p.lfo[0].rate_hz); return true; }
    if (n.path == "lfo[1].depth")   { scale(p.lfo[1].depth); return true; }
    if (n.path == "lfo[1].rate_hz") { scale(p.lfo[1].rate_hz); return true; }

    if (n.path == "master_gain") { scale(p.master_gain); return true; }

    return false;
}

// Walk the `nudges` JSON array (still raw, post-extract_text) and pull out
// { path, delta_percent } pairs. Tolerant: ignores entries that lack either
// field or use non-string/non-number types. Bounded to `max_count` to honour
// the system prompt's "Max 4" contract.
std::vector<Nudge> parseNudgeArray(const std::string& text, std::size_t max_count) {
    std::vector<Nudge> out;
    auto array_open = text.find("\"nudges\"");
    if (array_open == std::string::npos) return out;
    auto bracket = text.find('[', array_open);
    if (bracket == std::string::npos) return out;

    // Track brace depth so a nested object inside one entry doesn't confuse
    // the outer scanner.
    std::size_t i = bracket + 1;
    std::size_t depth = 0;
    std::size_t entry_start = i;
    bool inside_entry = false;
    auto flush_entry = [&](std::size_t from, std::size_t to) {
        if (out.size() >= max_count) return;
        const std::string entry = text.substr(from, to - from);
        Nudge n;
        n.path = find_string_field(entry, "\"path\"");
        const double d = find_number_field(entry, "\"delta_percent\"");
        if (n.path.empty() || std::isnan(d)) return;
        n.delta_percent = static_cast<float>(d);
        out.push_back(std::move(n));
    };

    while (i < text.size()) {
        const char c = text[i];
        if (c == '{') {
            if (depth == 0) { entry_start = i; inside_entry = true; }
            ++depth;
        } else if (c == '}') {
            if (depth > 0) --depth;
            if (depth == 0 && inside_entry) {
                flush_entry(entry_start, i + 1);
                inside_entry = false;
                if (out.size() >= max_count) break;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
        ++i;
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

std::string DeltaNudger::buildSystemPrompt() {
    // Kept compact on purpose — the user message carries 3 patch briefs which
    // burn most of the token budget. The whitelist + caps mirror lookupAxisCap
    // so the model sees exactly what applyNudges will enforce.
    return
        "You are a synth patch curator. Given a user's sound description and 3 candidate\n"
        "archetype patches, pick the BEST match and propose small parameter nudges to\n"
        "tailor it closer to the description. Output JSON only:\n"
        "\n"
        "{\n"
        "  \"selected_index\": 0 | 1 | 2,\n"
        "  \"nudges\": [\n"
        "    { \"path\": \"filter.cutoff_hz\", \"delta_percent\": -30..+30 },\n"
        "    { \"path\": \"amp_env.attack_s\", \"delta_percent\": -50..+50 },\n"
        "    { \"path\": \"reverb.mix\", \"delta_percent\": -30..+30 },\n"
        "    { \"path\": \"chorus.mix\", \"delta_percent\": -50..+50 },\n"
        "    { \"path\": \"tubesat.drive\", \"delta_percent\": -50..+50 },\n"
        "    { \"path\": \"lfo[0].depth\", \"delta_percent\": -50..+50 },\n"
        "    { \"path\": \"lfo[0].rate_hz\", \"delta_percent\": -30..+30 }\n"
        "  ],\n"
        "  \"rationale\": \"one sensory sentence\"\n"
        "}\n"
        "\n"
        "Whitelisted paths only. Max 4 nudges. Use small deltas (5-25%) — big jumps\n"
        "break archetypes. If the user prompt closely matches archetype 0/1/2 already,\n"
        "nudges can be empty.\n"
        "\n"
        "Do NOT propose nudges on: osc types, filter type, lfo target, osc enabled,\n"
        "voice_count. These are structural.\n";
}

std::string DeltaNudger::buildUserMessage(const NudgeRequest& req) {
    std::ostringstream o;
    o << "User prompt: \"" << req.prompt << "\"\n\n";
    for (std::size_t i = 0; i < req.top3.size() && i < 3; ++i) {
        const auto* a = req.top3[i];
        if (a == nullptr) continue;
        o << "Archetype " << i << " — " << a->name << ", tags: ";
        for (std::size_t t = 0; t < a->tags.size(); ++t) {
            if (t > 0) o << ", ";
            o << a->tags[t];
        }
        o << ":\n" << patchBriefJson(a->patch) << "\n\n";
    }
    o << "Output your selection now.";
    return o.str();
}

std::string DeltaNudger::http_post(const std::string& url, const std::string& json_body) const {
    TempFile req(json_body);
    std::ostringstream cmd;
    const int timeout_s = std::max(1, cfg_.timeout_ms / 1000);
    cmd << "curl --silent --show-error --fail-with-body"
        << " --max-time " << timeout_s
        << " -H 'Content-Type: application/json'"
        << " --data-binary @" << req.path << " '" << url << "'";

    std::string out;
    std::array<char, 4096> buf{};
#ifdef _WIN32
    FILE* p = _popen(cmd.str().c_str(), "r");
#else
    FILE* p = popen(cmd.str().c_str(), "r");
#endif
    if (!p) {
        std::cerr << "[DeltaNudger] popen failed for curl invocation\n";
        return {};
    }
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0)
        out.append(buf.data(), n);
#ifdef _WIN32
    const int rc = _pclose(p);
#else
    const int rc = pclose(p);
#endif
    if (rc != 0) {
        std::cerr << "[DeltaNudger] curl exited with non-zero status (raw=" << rc
                  << ", body_bytes=" << out.size() << ")\n";
        return {};
    }
    return out;
}

NudgeResult DeltaNudger::parseResponse(const std::string& response_body, int top3_size) {
    NudgeResult r;
    r.selected_index = -1;
    if (response_body.empty() || top3_size <= 0) return r;

    // Unwrap the Gemini envelope. The candidates[0].content.parts[0].text
    // field IS our JSON-only output per the system-prompt's "Output JSON
    // only" contract — but it ships JSON-escaped inside the outer envelope,
    // so we extract the string, unescape, then parse it as flat JSON.
    std::string inner = find_string_field(response_body, "\"text\"");
    if (inner.empty()) {
        // Some bodies (test seams, fenced output) deliver the inner JSON
        // directly — fall through and parse the raw body.
        inner = response_body;
    }

    // Strip optional ``` fences (markdown leak path).
    if (inner.size() >= 3 && inner.compare(0, 3, "```") == 0) {
        const auto nl = inner.find('\n');
        if (nl != std::string::npos) inner.erase(0, nl + 1);
        const auto fence = inner.rfind("```");
        if (fence != std::string::npos) inner.erase(fence);
    }

    const double idx = find_number_field(inner, "\"selected_index\"");
    if (std::isnan(idx)) return r;
    const int idx_i = static_cast<int>(idx);
    if (idx_i < 0 || idx_i >= top3_size) return r;

    r.selected_index = idx_i;
    r.nudges = parseNudgeArray(inner, /*max_count=*/4);
    r.rationale = find_string_field(inner, "\"rationale\"");
    return r;
}

namespace {

// Mirror of PatchValidator's per-field clamp ranges, narrowed to just the
// whitelisted nudgeable axes. Kept in this TU so the mapper layer doesn't
// have to link against engine_core (mapper sits BELOW engine in the static
// library dep graph; pulling engine in would invert the order). The values
// here MUST track engine/PatchValidator.cpp — if those ranges shift, this
// table needs to follow. Same source of truth for legal patch ranges, just
// duplicated at the API boundary for layering hygiene.
inline float clamp_f(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void clampNudgedFields(PatchStruct& p) noexcept {
    p.filter.cutoff_hz = clamp_f(p.filter.cutoff_hz, 20.0f, 18000.0f);
    p.filter.resonance = clamp_f(p.filter.resonance, 0.0f, 0.85f);
    p.filter.drive     = clamp_f(p.filter.drive, 0.0f, 1.0f);

    p.amp_env.attack_s  = clamp_f(p.amp_env.attack_s, 0.0f, 10.0f);
    p.amp_env.decay_s   = clamp_f(p.amp_env.decay_s, 0.0f, 10.0f);
    p.amp_env.release_s = clamp_f(p.amp_env.release_s, 0.0f, 20.0f);

    p.filter_env.attack_s = clamp_f(p.filter_env.attack_s, 0.0f, 10.0f);
    p.filter_env.decay_s  = clamp_f(p.filter_env.decay_s, 0.0f, 10.0f);

    p.reverb.size = clamp_f(p.reverb.size, 0.0f, 1.0f);
    p.reverb.mix  = clamp_f(p.reverb.mix, 0.0f, 1.0f);

    p.chorus.mix     = clamp_f(p.chorus.mix, 0.0f, 1.0f);
    p.chorus.depth   = clamp_f(p.chorus.depth, 0.0f, 1.0f);
    p.chorus.rate_hz = clamp_f(p.chorus.rate_hz, 0.1f, 5.0f);

    p.tubesat.drive = clamp_f(p.tubesat.drive, 0.0f, 0.5f);
    p.tubesat.mix   = clamp_f(p.tubesat.mix, 0.0f, 1.0f);

    for (int i = 0; i < kMaxLfos; ++i) {
        p.lfo[i].depth   = clamp_f(p.lfo[i].depth, 0.0f, 1.0f);
        p.lfo[i].rate_hz = clamp_f(p.lfo[i].rate_hz, 0.01f, 20.0f);
    }

    p.master_gain = clamp_f(p.master_gain, 0.0f, 1.0f);
}

} // namespace

PatchStruct applyNudges(const PatchStruct& base, const std::vector<Nudge>& nudges) noexcept {
    PatchStruct p = base;
    for (const auto& n : nudges) (void)applyOneNudge(p, n);
    // Clamp the nudged axes back to their engine-legal ranges. The downstream
    // pipeline (PromptHandler::refinePatch → PrePatchPipeline) runs the full
    // validate_patch on its own; this is the cheap pre-flight that keeps the
    // nudger's internal state sane.
    clampNudgedFields(p);
    return p;
}

NudgeResult DeltaNudger::nudge(const NudgeRequest& req) const {
    NudgeResult fail;
    fail.selected_index = -1;
    if (!req.top3.empty() && req.top3[0] != nullptr) fail.patch = req.top3[0]->patch;

    if (cfg_.api_key.empty()) {
        std::cerr << "[DeltaNudger] disabled: GEMINI_KEY not set\n";
        return fail;
    }
    if (req.top3.empty()) {
        std::cerr << "[DeltaNudger] empty top3 — nothing to score\n";
        return fail;
    }

    // ── Compose the request envelope ────────────────────────────────────────
    const std::string composed = buildSystemPrompt() + "\n\n" + buildUserMessage(req);
    const std::string& thr = cfg_.safety_threshold;
    std::ostringstream body_ss;
    body_ss << "{"
            << "\"contents\":[{\"parts\":[{\"text\":\"" << json_escape(composed) << "\"}]}],"
            << "\"generationConfig\":{"
            << "\"temperature\":" << cfg_.temperature << ","
            << "\"maxOutputTokens\":" << cfg_.max_output_tokens << ","
            << "\"thinkingConfig\":{\"thinkingBudget\":0},"
            << "\"responseMimeType\":\"application/json\""
            << "},"
            << "\"safetySettings\":["
            << "{\"category\":\"HARM_CATEGORY_HARASSMENT\",\"threshold\":\"" << thr << "\"},"
            << "{\"category\":\"HARM_CATEGORY_HATE_SPEECH\",\"threshold\":\"" << thr << "\"},"
            << "{\"category\":\"HARM_CATEGORY_SEXUALLY_EXPLICIT\",\"threshold\":\"" << thr << "\"},"
            << "{\"category\":\"HARM_CATEGORY_DANGEROUS_CONTENT\",\"threshold\":\"" << thr << "\"}"
            << "]"
            << "}";
    const std::string body = body_ss.str();
    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + cfg_.model +
                            ":generateContent?key=" + cfg_.api_key;

    // ── Retry loop (mirrors GeminiSampler) ──────────────────────────────────
    constexpr int kMaxAttempts = 3;
    constexpr std::array<int, kMaxAttempts - 1> kBackoffMs{200, 600};
    const auto t0 = std::chrono::steady_clock::now();

    std::string resp;
    int attempts = 0;
    std::string finish_reason;
    std::string block_reason;
    std::string outcome = "empty";
    const char* retry_reason = nullptr;
    bool give_up = false;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        attempts = attempt;
        resp = http_post(url, body);
        finish_reason.clear();
        block_reason.clear();
        retry_reason = nullptr;
        give_up = false;

        if (resp.empty()) {
            retry_reason = "empty body";
            outcome = "empty";
        } else {
            block_reason = find_string_field(resp, "\"blockReason\"");
            finish_reason = find_string_field(resp, "\"finishReason\"");
            const bool has_error = resp.find("\"error\"") != std::string::npos;

            if (!block_reason.empty() || finish_reason == "SAFETY") {
                outcome = "blocked";
                give_up = true;
            } else if (finish_reason == "MAX_TOKENS") {
                retry_reason = "finishReason=MAX_TOKENS";
                outcome = "truncated";
            } else if (has_error) {
                const std::string status = find_string_field(resp, "\"status\"");
                const bool transient = status == "UNAVAILABLE" || status == "INTERNAL" ||
                                       status == "DEADLINE_EXCEEDED";
                if (transient) {
                    retry_reason = "transient 5xx in body";
                    outcome = "curl_error";
                } else {
                    outcome = "blocked";
                    give_up = true;
                }
            } else {
                outcome = "success"; // refined after parse
            }
        }

        if (give_up || retry_reason == nullptr) break;
        if (attempt < kMaxAttempts) {
            const int delay = kBackoffMs[static_cast<std::size_t>(attempt - 1)];
            std::cerr << "[DeltaNudger] retry " << attempt << "/" << kMaxAttempts
                      << " after " << delay << "ms (reason: " << retry_reason << ")\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        } else {
            std::cerr << "[DeltaNudger] giving up after " << kMaxAttempts
                      << " attempts (final reason: " << retry_reason << ")\n";
        }
    }

    auto emit_telemetry = [&](const std::string& final_outcome) {
        const auto t1 = std::chrono::steady_clock::now();
        const double latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        LlmCall rec;
        rec.caller = "DeltaNudger";
        rec.model = cfg_.model;
        rec.attempts = attempts;
        rec.latency_ms = latency_ms;
        rec.body_size_bytes = resp.size();
        rec.prompt_size_bytes = body.size();
        rec.finish_reason = finish_reason;
        rec.block_reason = block_reason;
        rec.outcome = final_outcome;
        LlmTelemetry::instance().log(rec);
    };

    NudgeResult parsed = parseResponse(resp, static_cast<int>(req.top3.size()));
    if (parsed.selected_index < 0) {
        std::cerr << "[DeltaNudger] parse failed or selected_index out of range\n";
        emit_telemetry(outcome == "success" ? "empty" : outcome);
        return fail;
    }

    const auto* sel = req.top3[static_cast<std::size_t>(parsed.selected_index)];
    if (sel == nullptr) {
        emit_telemetry("empty");
        return fail;
    }

    parsed.patch = applyNudges(sel->patch, parsed.nudges);
    emit_telemetry("success");
    return parsed;
}

} // namespace agentic_synth::mapper
