#include "mapper/GeminiSampler.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "mapper/GrammarSampler.h"

namespace agentic_synth::mapper {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
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

// Unescape a JSON string body (no surrounding quotes, no nested objects).
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
        case '"':
            out += '"';
            break;
        case '\\':
            out += '\\';
            break;
        case '/':
            out += '/';
            break;
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case 'b':
            out += '\b';
            break;
        case 'f':
            out += '\f';
            break;
        case 'u': {
            if (i + 4 >= s.size())
                return out;
            const std::string hex = s.substr(i + 1, 4);
            i += 4;
            try {
                const auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                // Lossy: synth-domain JSON is ASCII; non-ASCII shouldn't appear
                // in patch values. Anything > 0x7F is dropped to keep the
                // parser path simple. If it ever matters we can swap in UTF-8.
                if (cp < 0x80)
                    out += static_cast<char>(cp);
            } catch (...) {
                // ignore malformed escape
            }
            break;
        }
        default:
            out += n;
            break;
        }
    }
    return out;
}

// Tempfile guard: writes content on construction, deletes on destruction.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& contents) {
        const char* tmp = std::getenv("TMPDIR");
        if (tmp == nullptr || *tmp == 0)
            tmp = "/tmp";
        std::mt19937_64 rng{std::random_device{}()};
        const auto rnd = rng();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/gemini_req_%llx.json", static_cast<unsigned long long>(rnd));
        path = std::string(tmp) + buf;
        std::ofstream f(path, std::ios::binary);
        f << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

} // namespace

GeminiSampler::GeminiSampler(GeminiSamplerConfig cfg) : cfg_(std::move(cfg)) {}

std::string GeminiSampler::build_request(const std::string& user_prompt, uint32_t patch_id) const {
    const std::string base_prompt =
        cfg_.system_prompt.empty()
            ? std::string("You are a synthesizer patch designer. Output ONLY a strict JSON object matching the "
                          "synth's PatchStruct schema — no prose, no markdown fences.")
            : cfg_.system_prompt;

    const std::string composed = base_prompt + "\n\nGenerate a JSON patch (patch_id=" + std::to_string(patch_id) +
                                 ") for: " + user_prompt +
                                 "\n\nReturn JSON only. No markdown. No prose. Match the schema exactly.";

    std::ostringstream body;
    body << "{"
         << "\"contents\":[{\"parts\":[{\"text\":\"" << json_escape(composed) << "\"}]}],"
         << "\"generationConfig\":{"
         << "\"temperature\":" << cfg_.temperature << ","
         << "\"responseMimeType\":\"application/json\""
         << "}"
         << "}";
    return body.str();
}

std::string GeminiSampler::http_post(const std::string& url, const std::string& json_body) const {
    // Stash the request body in a temp file so we don't have to shell-escape
    // the JSON. curl reads it via `--data-binary @file`.
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
    if (!p)
        return {};
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0)
        out.append(buf.data(), n);
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return out;
}

std::string GeminiSampler::extract_text(const std::string& resp) {
    // We expect: {"candidates":[{"content":{"parts":[{"text":"...JSON..."}]}}]}
    // Find the first "text": "..." occurrence and unescape its body.
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

std::optional<PatchStruct> GeminiSampler::generate(const std::string& user_prompt, uint32_t patch_id) const {
    if (cfg_.api_key.empty()) {
        std::cerr << "[GeminiSampler] disabled: GEMINI_KEY not set\n";
        return std::nullopt;
    }

    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + cfg_.model +
                            ":generateContent?key=" + cfg_.api_key;
    const std::string body = build_request(user_prompt, patch_id);
    const std::string resp = http_post(url, body);
    if (resp.empty()) {
        std::cerr << "[GeminiSampler] empty response from " << cfg_.model << "\n";
        return std::nullopt;
    }
    std::string text = extract_text(resp);
    if (text.empty()) {
        std::cerr << "[GeminiSampler] could not extract candidates[0].content.parts[0].text from response\n";
        return std::nullopt;
    }
    // Some Gemini outputs still wrap JSON in ``` fences despite the
    // responseMimeType hint. Strip them defensively before validation.
    if (text.size() >= 3 && text.compare(0, 3, "```") == 0) {
        const auto nl = text.find('\n');
        if (nl != std::string::npos)
            text.erase(0, nl + 1);
        const auto fence = text.rfind("```");
        if (fence != std::string::npos)
            text.erase(fence);
    }

    auto patch = GrammarSampler::parse_patch_json(text);
    if (!patch) {
        std::cerr << "[GeminiSampler] parse_patch_json rejected response (likely schema drift or range "
                     "violation)\n";
        return std::nullopt;
    }
    return patch;
}

} // namespace agentic_synth::mapper
