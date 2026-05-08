#include "mapper/SemanticMapper.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace agentic_synth::mapper {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SemanticMapper::SemanticMapper(SemanticMapperConfig cfg) : cfg_(std::move(cfg)) {}

// ---------------------------------------------------------------------------
// Tokenisation
// ---------------------------------------------------------------------------

std::vector<std::string> SemanticMapper::tokenise(const std::string& prompt) {
    std::vector<std::string> tokens;
    std::string tok;
    for (unsigned char c : prompt) {
        if (std::isalpha(c)) {
            tok += static_cast<char>(std::tolower(c));
        } else {
            if (!tok.empty()) { tokens.push_back(tok); tok.clear(); }
        }
    }
    if (!tok.empty()) tokens.push_back(tok);
    return tokens;
}

// ---------------------------------------------------------------------------
// Context inference
// ---------------------------------------------------------------------------

SoundContext SemanticMapper::infer_context(const std::string& prompt) {
    static const std::unordered_map<std::string, SoundContext> kContextWords = {
        {"bass",      SoundContext::Bass},
        {"sub",       SoundContext::Bass},
        {"808",       SoundContext::Bass},
        {"pad",       SoundContext::Pad},
        {"strings",   SoundContext::Pad},
        {"texture",   SoundContext::Texture},
        {"drone",     SoundContext::Texture},
        {"lead",      SoundContext::Lead},
        {"solo",      SoundContext::Lead},
        {"melody",    SoundContext::Lead},
        {"keys",      SoundContext::Keys},
        {"piano",     SoundContext::Keys},
        {"organ",     SoundContext::Keys},
        {"ep",        SoundContext::Keys},
        {"perc",      SoundContext::Percussion},
        {"drum",      SoundContext::Percussion},
        {"hit",       SoundContext::Percussion},
        {"arp",       SoundContext::Arp},
        {"sequence",  SoundContext::Arp},
        {"pluck",     SoundContext::Arp},
    };

    for (const auto& tok : tokenise(prompt)) {
        auto it = kContextWords.find(tok);
        if (it != kContextWords.end()) return it->second;
    }
    return SoundContext::Generic;
}

// ---------------------------------------------------------------------------
// Word-overlap similarity (n-gram + exact)
// ---------------------------------------------------------------------------

float SemanticMapper::word_overlap_score(const std::string& query,
                                          std::string_view   kw) noexcept {
    if (query == kw) return 1.0f;
    // Prefix match bonus
    const size_t min_len = std::min(query.size(), kw.size());
    size_t match = 0;
    for (size_t i = 0; i < min_len; ++i) {
        if (query[i] == kw[i]) ++match; else break;
    }
    const float prefix_score = static_cast<float>(match) / static_cast<float>(std::max(query.size(), kw.size()));
    // Character bigram overlap
    auto bigrams = [](std::string_view s, std::vector<std::string>& out) {
        for (size_t i = 0; i + 1 < s.size(); ++i)
            out.push_back(std::string{s[i], s[i+1]});
    };
    std::vector<std::string> q_bg, k_bg;
    bigrams(query, q_bg);
    bigrams(kw, k_bg);
    if (q_bg.empty() || k_bg.empty()) return prefix_score;
    int common = 0;
    for (auto& b : q_bg)
        if (std::find(k_bg.begin(), k_bg.end(), b) != k_bg.end()) ++common;
    const float bigram_score = 2.0f * static_cast<float>(common) /
                               static_cast<float>(q_bg.size() + k_bg.size());
    return 0.4f * prefix_score + 0.6f * bigram_score;
}

// ---------------------------------------------------------------------------
// Cosine similarity
// ---------------------------------------------------------------------------

float SemanticMapper::cosine(const std::vector<float>& a,
                              const std::vector<float>& b) noexcept {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    const float denom = std::sqrt(na) * std::sqrt(nb);
    return (denom > 1e-8f) ? dot / denom : 0.0f;
}

// ---------------------------------------------------------------------------
// HTTP POST to llama.cpp /embedding
// ---------------------------------------------------------------------------

std::string SemanticMapper::http_post_embedding(const std::string& text) const {
    if (cfg_.server_url.empty()) return {};

    // Parse URL
    const std::string prefix = "http://";
    if (cfg_.server_url.rfind(prefix, 0) != 0) return {};
    std::string rest = cfg_.server_url.substr(prefix.size());
    auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string host;
    int port = 80;
    auto colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = std::stoi(hostport.substr(colon + 1));
    } else {
        host = hostport;
    }
    const std::string path = "/embedding";

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        return {};

    int fd = static_cast<int>(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (fd < 0) { freeaddrinfo(res); return {}; }
    if (connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
#ifndef _WIN32
        close(fd);
#else
        closesocket(fd);
#endif
        return {};
    }
    freeaddrinfo(res);

    struct timeval tv{};
    tv.tv_sec  = cfg_.timeout_ms / 1000;
    tv.tv_usec = (cfg_.timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // Escape text for JSON
    std::string escaped;
    for (char c : text) {
        if (c == '"')       escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else                escaped += c;
    }
    const std::string body = "{\"content\":\"" + escaped + "\"}";

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.0\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    const std::string rstr = req.str();
    send(fd, rstr.data(), static_cast<int>(rstr.size()), 0);

    std::string resp;
    char buf[4096];
    int n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) resp.append(buf, static_cast<size_t>(n));
#ifndef _WIN32
    close(fd);
#else
    closesocket(fd);
#endif

    auto sep = resp.find("\r\n\r\n");
    return (sep == std::string::npos) ? resp : resp.substr(sep + 4);
}

// ---------------------------------------------------------------------------
// Parse embedding JSON: {"embedding": [f, f, f, ...]}
// ---------------------------------------------------------------------------

std::vector<float> SemanticMapper::parse_embedding_json(const std::string& json) {
    std::vector<float> result;
    const std::string key = "\"embedding\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return result;
    pos = json.find('[', pos + key.size());
    if (pos == std::string::npos) return result;
    ++pos;
    while (pos < json.size() && json[pos] != ']') {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n')) ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        const char* begin = json.data() + pos;
        char* end = nullptr;
        float v = std::strtof(begin, &end);
        if (end == begin) break;
        result.push_back(v);
        pos = static_cast<size_t>(end - json.data());
    }
    return result;
}

// ---------------------------------------------------------------------------
// fetch_embedding — calls server or returns empty (fallback to word-overlap)
// ---------------------------------------------------------------------------

std::vector<float> SemanticMapper::fetch_embedding(const std::string& text) const {
    if (cfg_.server_url.empty()) return {};
    const std::string resp = http_post_embedding(text);
    if (resp.empty()) return {};
    return parse_embedding_json(resp);
}

// ---------------------------------------------------------------------------
// best_match — find closest DescriptorEntry for one word
// ---------------------------------------------------------------------------

std::optional<const DescriptorEntry*>
SemanticMapper::best_match(const std::string& descriptor, SoundContext ctx) const {
    const auto& dataset = get_descriptor_dataset();

    // Attempt embedding similarity if server available
    const auto query_emb = fetch_embedding(descriptor);
    const bool use_embedding = (query_emb.size() == static_cast<size_t>(cfg_.embedding_dims));

    const DescriptorEntry* best_ctx     = nullptr;  // best context-specific match
    const DescriptorEntry* best_generic = nullptr;  // best generic match
    float best_ctx_score     = -1.0f;
    float best_generic_score = -1.0f;

    for (const auto& entry : dataset) {
        float score = 0.0f;

        if (use_embedding) {
            // Fetch embedding for keyword (cached in a real implementation)
            auto kw_emb = fetch_embedding(std::string(entry.keyword));
            if (kw_emb.size() == query_emb.size())
                score = cosine(query_emb, kw_emb);
            else
                score = word_overlap_score(descriptor, entry.keyword);
        } else {
            score = word_overlap_score(descriptor, entry.keyword);
        }

        if (score < cfg_.similarity_threshold) continue;

        if (entry.context == ctx && score > best_ctx_score) {
            best_ctx_score = score;
            best_ctx       = &entry;
        } else if (entry.context == SoundContext::Generic && score > best_generic_score) {
            best_generic_score = score;
            best_generic       = &entry;
        }
    }

    // Prefer context-specific match; fall back to generic
    if (best_ctx)     return best_ctx;
    if (best_generic) return best_generic;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// apply — main entry point
// ---------------------------------------------------------------------------

int SemanticMapper::apply(const std::string& prompt, PatchStruct& patch) const {
    const SoundContext ctx    = infer_context(prompt);
    const auto         tokens = tokenise(prompt);
    int matched = 0;

    for (const auto& tok : tokens) {
        auto entry = best_match(tok, ctx);
        if (!entry) continue;
        apply_delta(patch, (*entry)->delta);
        ++matched;
    }
    return matched;
}

} // namespace agentic_synth::mapper
