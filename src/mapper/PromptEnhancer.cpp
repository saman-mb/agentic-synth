#include "mapper/PromptEnhancer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include "mapper/LlmTelemetry.h"
#include "mapper/PromptSanitizer.h"

namespace agentic_synth::mapper {

namespace {

// JSON escape — mirrors GeminiSampler's helper. Translator output is plain
// text not JSON, but the *request* still puts the composed prompt inside a
// JSON string literal, so escape on the way out.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
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

// JSON string unescape (no nested objects, content body only).
std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        if (i + 1 >= s.size())
            break;
        const char n = s[++i];
        switch (n) {
        case '"':  out += '"'; break;
        case '\\': out += '\\'; break;
        case '/':  out += '/'; break;
        case 'n':  out += '\n'; break;
        case 't':  out += '\t'; break;
        case 'r':  out += '\r'; break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'u': {
            if (i + 4 >= s.size())
                return out;
            const std::string hex = s.substr(i + 1, 4);
            i += 4;
            try {
                const auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                if (cp < 0x80)
                    out += static_cast<char>(cp);
            } catch (...) {
                // ignore malformed escape
            }
            break;
        }
        default: out += n; break;
        }
    }
    return out;
}

// Tempfile RAII — write contents on construction, delete on destruction.
// Copied from GeminiSampler so curl can read the body via --data-binary @
// without shell-escaping the JSON.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& contents) {
        const char* tmp = std::getenv("TMPDIR");
        if (tmp == nullptr || *tmp == 0)
            tmp = "/tmp";
        std::mt19937_64 rng{std::random_device{}()};
        const auto rnd = rng();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/enhancer_req_%llx.json", static_cast<unsigned long long>(rnd));
        path = std::string(tmp) + buf;
        std::ofstream f(path, std::ios::binary);
        f << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

} // namespace

PromptEnhancer::PromptEnhancer(PromptEnhancerConfig cfg) : cfg_(std::move(cfg)) {}

std::string PromptEnhancer::loadEnhancerPromptFile(const std::string& override_path) {
    auto try_read = [](const std::string& path) -> std::string {
        if (path.empty())
            return {};
        std::ifstream f(path);
        if (!f)
            return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    if (auto txt = try_read(override_path); !txt.empty())
        return txt;

#ifdef AGENTIC_SYNTH_ENHANCER_PROMPT_PATH
    if (auto txt = try_read(AGENTIC_SYNTH_ENHANCER_PROMPT_PATH); !txt.empty())
        return txt;
#endif

    // Dev fallback — run from repo root.
    if (auto txt = try_read("src/mapper/enhancer-prompt.md"); !txt.empty())
        return txt;

    return {};
}

std::string PromptEnhancer::canonicalise(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool prev_space = true; // strips leading whitespace
    for (char c : s) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!prev_space)
                out += ' ';
            prev_space = true;
        } else {
            out += static_cast<char>(std::tolower(uc));
            prev_space = false;
        }
    }
    // Strip trailing single space if collapsing left one behind.
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

void PromptEnhancer::cache_put_locked(const std::string& key, std::string value) const {
    // If key already present, update value + promote to front and return.
    auto it = cache_idx_.find(key);
    if (it != cache_idx_.end()) {
        it->second->second = std::move(value);
        cache_lru_.splice(cache_lru_.begin(), cache_lru_, it->second);
        return;
    }
    cache_lru_.emplace_front(key, std::move(value));
    cache_idx_[key] = cache_lru_.begin();
    if (cache_lru_.size() > kCacheCap) {
        const auto& back = cache_lru_.back();
        cache_idx_.erase(back.first);
        cache_lru_.pop_back();
    }
}

std::string PromptEnhancer::cache_get_locked(const std::string& key) const {
    auto it = cache_idx_.find(key);
    if (it == cache_idx_.end())
        return {};
    // Touch — promote to front (most-recently-used).
    cache_lru_.splice(cache_lru_.begin(), cache_lru_, it->second);
    return it->second->second;
}

std::string PromptEnhancer::http_post(const std::string& url, const std::string& json_body) const {
    int unused = 0;
    return http_post_ex(url, json_body, unused);
}

std::string PromptEnhancer::http_post_ex(const std::string& url, const std::string& json_body,
                                         int& exit_code) const {
    TempFile req(json_body);

    std::ostringstream cmd;
    const int timeout_s = std::max(1, cfg_.timeout_ms / 1000);
    cmd << "curl --silent --show-error --fail-with-body"
        << " --max-time " << timeout_s
        << " -H 'Content-Type: application/json'"
        << " --data-binary @" << req.path << " '" << url << "' 2>/dev/null";

    std::string out;
    std::array<char, 4096> buf{};
#ifdef _WIN32
    FILE* p = _popen(cmd.str().c_str(), "r");
#else
    FILE* p = popen(cmd.str().c_str(), "r");
#endif
    if (!p) {
        exit_code = -1;
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
    exit_code = rc;
    return out;
}

namespace {

// Cheap probe: find "key":"value" — quotes-included key — and return value.
// Mirrors GeminiSampler's find_string_field but kept local to avoid
// cross-TU header gymnastics. Used by the retry loop to peek at
// finishReason / blockReason without invoking the full extractor.
std::string find_string_field(const std::string& resp, const std::string& key) {
    auto pos = resp.find(key);
    if (pos == std::string::npos)
        return {};
    pos += key.size();
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':'))
        ++pos;
    if (pos >= resp.size() || resp[pos] != '"')
        return {};
    ++pos;
    std::string raw;
    while (pos < resp.size()) {
        if (resp[pos] == '\\') {
            if (pos + 1 >= resp.size())
                break;
            raw += resp[pos];
            raw += resp[pos + 1];
            pos += 2;
            continue;
        }
        if (resp[pos] == '"')
            break;
        raw += resp[pos++];
    }
    return raw;
}

} // namespace

std::string PromptEnhancer::extract_text(const std::string& resp) {
    // {"candidates":[{"content":{"parts":[{"text":"...brief..."}]}}]}
    const std::string key = "\"text\"";
    auto pos = resp.find(key);
    if (pos == std::string::npos)
        return {};
    pos += key.size();
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':'))
        ++pos;
    if (pos >= resp.size() || resp[pos] != '"')
        return {};
    ++pos;
    std::string raw;
    while (pos < resp.size()) {
        if (resp[pos] == '\\') {
            if (pos + 1 >= resp.size())
                break;
            raw += resp[pos];
            raw += resp[pos + 1];
            pos += 2;
            continue;
        }
        if (resp[pos] == '"')
            break;
        raw += resp[pos++];
    }
    return json_unescape(raw);
}

std::string PromptEnhancer::enhance(const std::string& userPrompt) const {
    if (userPrompt.empty()) {
        // Translator prompt instructs the model to fall back to "neutral
        // warm pad" on empty input — but if the *user* gave us nothing,
        // the right call is to skip the network round-trip entirely and
        // let the generator handle the empty path.
        return {};
    }

    if (cfg_.api_key.empty()) {
        std::cerr << "[PromptEnhancer] disabled: GEMINI_KEY not set\n";
        return {};
    }

    const std::string cacheKey = canonicalise(userPrompt);
    {
        std::lock_guard<std::mutex> lock(cache_mu_);
        if (auto hit = cache_get_locked(cacheKey); !hit.empty()) {
            std::cerr << "[PromptEnhancer] cache hit (key='" << cacheKey << "')\n";
            return hit;
        }
    }

    // Phase 33 — soften safety triggers before sending. Cache key is the
    // canonicalised ORIGINAL prompt (so the user gets the same brief for
    // "horror pad" twice in a row even after sanitisation), but the wire
    // payload uses the rewritten form.
    const std::string sanitized = sanitizePromptForSafety(userPrompt);
    if (sanitized != userPrompt) {
        std::cerr << "[PromptEnhancer] sanitized prompt for safety filters (trigger words rewritten)\n";
    }

    const std::string base_prompt =
        cfg_.system_prompt.empty()
            ? std::string("You are TIMBRE, a translator that rewrites a terse sound description into a 9-section "
                          "plain-text sound-design brief. No JSON, no markdown.")
            : cfg_.system_prompt;

    const std::string composed = base_prompt + "\n\nProducer prompt: " + sanitized +
                                 "\n\nEmit the brief now, starting at SONIC CHARACTER:";

    std::ostringstream body;
    body << "{"
         << "\"contents\":[{\"parts\":[{\"text\":\"" << json_escape(composed) << "\"}]}],"
         << "\"generationConfig\":{"
         << "\"temperature\":" << cfg_.temperature
         // Deliberately NO responseMimeType — translator output is free-form
         // plain text, not JSON. Setting application/json here would make
         // the model wrap the brief in a quoted string and fight us.
         << "}"
         << "}";
    const std::string bodyStr = body.str();

    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + cfg_.model +
                            ":generateContent?key=" + cfg_.api_key;

    // Phase 33 — retry with exponential backoff (200 / 600 / 1800 ms).
    // Same retry policy as GeminiSampler::generate; the translator path
    // hits the same backend and benefits from the same transient-failure
    // recovery. NEVER retry on SAFETY / non-5xx error.
    constexpr int kMaxAttempts = 3;
    constexpr std::array<int, kMaxAttempts - 1> kBackoffMs{200, 600};
    const auto t0 = std::chrono::steady_clock::now();

    std::string resp;
    int curl_rc = 0;
    int attempts = 0;
    std::string finish_reason;
    std::string block_reason;
    std::string outcome;
    const char* retry_reason = nullptr;
    bool give_up = false;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        attempts = attempt;
        curl_rc = 0;
        resp = http_post_ex(url, bodyStr, curl_rc);
        finish_reason.clear();
        block_reason.clear();
        retry_reason = nullptr;

        if (curl_rc != 0) {
            retry_reason = "curl exit non-zero";
            outcome = "curl_error";
        } else if (resp.empty()) {
            retry_reason = "empty body";
            outcome = "empty";
        } else {
            block_reason = find_string_field(resp, "\"blockReason\"");
            finish_reason = find_string_field(resp, "\"finishReason\"");
            const bool has_error_envelope = resp.find("\"error\"") != std::string::npos;

            if (!block_reason.empty() || finish_reason == "SAFETY") {
                outcome = "blocked";
                give_up = true;
            } else if (finish_reason == "MAX_TOKENS") {
                retry_reason = "finishReason=MAX_TOKENS";
                outcome = "truncated";
            } else if (has_error_envelope) {
                const std::string status = find_string_field(resp, "\"status\"");
                const bool transient_5xx = resp.find("\"code\": 5") != std::string::npos ||
                                           resp.find("\"code\":5")  != std::string::npos ||
                                           status == "UNAVAILABLE" || status == "INTERNAL" ||
                                           status == "DEADLINE_EXCEEDED";
                if (transient_5xx) {
                    retry_reason = "transient 5xx in body";
                    outcome = "curl_error";
                } else {
                    outcome = "blocked";
                    give_up = true;
                }
            } else {
                outcome = "success";
            }
        }

        if (give_up || retry_reason == nullptr)
            break;

        if (attempt < kMaxAttempts) {
            const int delay = kBackoffMs[static_cast<std::size_t>(attempt - 1)];
            std::cerr << "[PromptEnhancer] retry " << attempt << "/" << kMaxAttempts
                      << " after " << delay << "ms (reason: " << retry_reason << ")\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        } else {
            std::cerr << "[PromptEnhancer] giving up after " << kMaxAttempts
                      << " attempts (final reason: " << retry_reason << ")\n";
        }
    }

    auto emit_telemetry = [&](const std::string& final_outcome) {
        const auto t1 = std::chrono::steady_clock::now();
        const double latency_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        LlmCall rec;
        rec.caller = "PromptEnhancer";
        rec.model = cfg_.model;
        rec.attempts = attempts;
        rec.latency_ms = latency_ms;
        rec.body_size_bytes = resp.size();
        rec.prompt_size_bytes = bodyStr.size();
        rec.finish_reason = finish_reason;
        rec.block_reason = block_reason;
        rec.outcome = final_outcome;
        LlmTelemetry::instance().log(rec);
    };

    if (resp.empty()) {
        std::cerr << "[PromptEnhancer] empty response from " << cfg_.model << "\n";
        emit_telemetry("empty");
        return {};
    }

    std::string text = extract_text(resp);
    if (text.empty()) {
        std::cerr << "[PromptEnhancer] could not extract candidates[0].content.parts[0].text\n";
        emit_telemetry(outcome.empty() ? "empty" : outcome);
        return {};
    }

    // Some models still wrap text in ``` fences despite the instruction;
    // strip them defensively before the brief reaches the UI.
    if (text.size() >= 3 && text.compare(0, 3, "```") == 0) {
        const auto nl = text.find('\n');
        if (nl != std::string::npos)
            text.erase(0, nl + 1);
        const auto fence = text.rfind("```");
        if (fence != std::string::npos)
            text.erase(fence);
    }

    {
        std::lock_guard<std::mutex> lock(cache_mu_);
        cache_put_locked(cacheKey, text);
    }
    emit_telemetry("success");
    return text;
}

} // namespace agentic_synth::mapper
