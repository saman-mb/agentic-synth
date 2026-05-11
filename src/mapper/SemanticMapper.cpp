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

#include <juce_core/juce_core.h>

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

SemanticMapper::SemanticMapper(SemanticMapperConfig cfg) : cfg_(std::move(cfg)) {
    // Phase 9B: prewarm only when a server is configured. Offline path used
    // by unit tests stays a pure no-op; we never block the constructor on
    // network calls when server_url is empty.
    if (!cfg_.server_url.empty())
        prewarmEmbeddings();
}

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
            if (!tok.empty()) {
                tokens.push_back(tok);
                tok.clear();
            }
        }
    }
    if (!tok.empty())
        tokens.push_back(tok);
    return tokens;
}

// ---------------------------------------------------------------------------
// Context inference
// ---------------------------------------------------------------------------

SoundContext SemanticMapper::infer_context(const std::string& prompt) {
    static const std::unordered_map<std::string, SoundContext> kContextWords = {
        {"bass", SoundContext::Bass},       {"sub", SoundContext::Bass},       {"808", SoundContext::Bass},
        {"pad", SoundContext::Pad},         {"strings", SoundContext::Pad},    {"texture", SoundContext::Texture},
        {"drone", SoundContext::Texture},   {"lead", SoundContext::Lead},      {"solo", SoundContext::Lead},
        {"melody", SoundContext::Lead},     {"keys", SoundContext::Keys},      {"piano", SoundContext::Keys},
        {"organ", SoundContext::Keys},      {"ep", SoundContext::Keys},        {"perc", SoundContext::Percussion},
        {"drum", SoundContext::Percussion}, {"hit", SoundContext::Percussion}, {"arp", SoundContext::Arp},
        {"sequence", SoundContext::Arp},    {"pluck", SoundContext::Arp},
    };

    for (const auto& tok : tokenise(prompt)) {
        auto it = kContextWords.find(tok);
        if (it != kContextWords.end())
            return it->second;
    }
    return SoundContext::Generic;
}

// ---------------------------------------------------------------------------
// Word-overlap similarity (n-gram + exact)
// ---------------------------------------------------------------------------

float SemanticMapper::word_overlap_score(const std::string& query, std::string_view kw) noexcept {
    if (query == kw)
        return 1.0f;
    // Prefix match bonus
    const size_t min_len = std::min(query.size(), kw.size());
    size_t match = 0;
    for (size_t i = 0; i < min_len; ++i) {
        if (query[i] == kw[i])
            ++match;
        else
            break;
    }
    const float prefix_score = static_cast<float>(match) / static_cast<float>(std::max(query.size(), kw.size()));
    // Character bigram overlap
    auto bigrams = [](std::string_view s, std::vector<std::string>& out) {
        for (size_t i = 0; i + 1 < s.size(); ++i)
            out.push_back(std::string{s[i], s[i + 1]});
    };
    std::vector<std::string> q_bg, k_bg;
    bigrams(query, q_bg);
    bigrams(kw, k_bg);
    if (q_bg.empty() || k_bg.empty())
        return prefix_score;
    int common = 0;
    for (auto& b : q_bg)
        if (std::find(k_bg.begin(), k_bg.end(), b) != k_bg.end())
            ++common;
    const float bigram_score = 2.0f * static_cast<float>(common) / static_cast<float>(q_bg.size() + k_bg.size());
    return 0.4f * prefix_score + 0.6f * bigram_score;
}

// ---------------------------------------------------------------------------
// Cosine similarity
// ---------------------------------------------------------------------------

float SemanticMapper::cosine(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size() || a.empty())
        return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    const float denom = std::sqrt(na) * std::sqrt(nb);
    return (denom > 1e-8f) ? dot / denom : 0.0f;
}

// ---------------------------------------------------------------------------
// HTTP POST to llama.cpp /embedding
// ---------------------------------------------------------------------------

std::string SemanticMapper::http_post_embedding(const std::string& text) const {
    if (cfg_.server_url.empty())
        return {};

    // Parse URL
    const std::string prefix = "http://";
    if (cfg_.server_url.rfind(prefix, 0) != 0)
        return {};
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
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        return {};

    int fd = static_cast<int>(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (fd < 0) {
        freeaddrinfo(res);
        return {};
    }
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
    tv.tv_sec = cfg_.timeout_ms / 1000;
    tv.tv_usec = (cfg_.timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // Escape text for JSON
    std::string escaped;
    for (char c : text) {
        if (c == '"')
            escaped += "\\\"";
        else if (c == '\\')
            escaped += "\\\\";
        else if (c == '\n')
            escaped += "\\n";
        else
            escaped += c;
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
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
#ifndef _WIN32
    close(fd);
#else
    closesocket(fd);
#endif

    auto sep = resp.find("\r\n\r\n");
    return (sep == std::string::npos) ? resp : resp.substr(sep + 4);
}

// ---------------------------------------------------------------------------
// Parse embedding JSON. llama.cpp /embedding can return either:
//   {"embedding": [f, f, ...]}
//   [{"embedding": [f, f, ...]}, ...]   (server-mode batched response)
//   {"embedding": [[f, f, ...]]}         (nested when encoding_format is set)
// All three shapes are handled via juce::var traversal so we no longer
// substring-scan the raw response body.
// ---------------------------------------------------------------------------

std::vector<float> SemanticMapper::parse_embedding_json(const std::string& json) {
    std::vector<float> result;
    const auto parsed = juce::JSON::parse(juce::String(json));
    if (parsed.isVoid())
        return result;

    // Unwrap top-level array → first element.
    juce::var node = parsed;
    if (auto* arr = node.getArray()) {
        if (arr->isEmpty())
            return result;
        node = arr->getReference(0);
    }

    auto* obj = node.getDynamicObject();
    if (obj == nullptr)
        return result;

    juce::var emb = obj->getProperty("embedding");
    auto* embArr = emb.getArray();
    if (embArr == nullptr)
        return result;

    // Nested form: [[f, f, ...]] → unwrap one level.
    if (!embArr->isEmpty() && embArr->getReference(0).isArray())
        embArr = embArr->getReference(0).getArray();
    if (embArr == nullptr)
        return result;

    result.reserve(static_cast<size_t>(embArr->size()));
    for (const auto& v : *embArr)
        result.push_back(static_cast<float>(static_cast<double>(v)));
    return result;
}

// ---------------------------------------------------------------------------
// fetch_embedding — calls server or returns empty (fallback to word-overlap).
//
// Phase 9B: results are memoised in embeddingCache_ for the SemanticMapper's
// lifetime. Mutex guards only the cache read/write; the HTTP call itself
// runs outside the lock so concurrent misses still issue parallel network
// requests instead of serialising on the cache mutex.
// ---------------------------------------------------------------------------

std::vector<float> SemanticMapper::fetch_embedding(const std::string& text) const {
    if (cfg_.server_url.empty())
        return {};

    {
        std::lock_guard<std::mutex> lock(embeddingCacheMutex_);
        const auto it = embeddingCache_.find(text);
        if (it != embeddingCache_.end())
            return it->second;
    }

    const std::string resp = http_post_embedding(text);
    std::vector<float> emb;
    if (!resp.empty())
        emb = parse_embedding_json(resp);

    {
        std::lock_guard<std::mutex> lock(embeddingCacheMutex_);
        // Insert even an empty vector so transient server failures don't
        // hammer the endpoint repeatedly within one session. Caller already
        // treats an empty vector as "fall back to word-overlap".
        embeddingCache_.emplace(text, emb);
    }
    return emb;
}

// ---------------------------------------------------------------------------
// prewarmEmbeddings — populate the cache for every static descriptor keyword
// in one synchronous pass at construction time. No-op when offline.
// ---------------------------------------------------------------------------

void SemanticMapper::prewarmEmbeddings() {
    if (cfg_.server_url.empty())
        return;
    for (const auto& entry : get_descriptor_dataset())
        (void)fetch_embedding(std::string(entry.keyword));
}

// ---------------------------------------------------------------------------
// best_match — find closest DescriptorEntry for one word
// ---------------------------------------------------------------------------

std::optional<const DescriptorEntry*> SemanticMapper::best_match(const std::string& descriptor,
                                                                 SoundContext ctx) const {
    const auto& dataset = get_descriptor_dataset();

    // Attempt embedding similarity if server available
    const auto query_emb = fetch_embedding(descriptor);
    const bool use_embedding = (query_emb.size() == static_cast<size_t>(cfg_.embedding_dims));

    const DescriptorEntry* best_ctx = nullptr;     // best context-specific match
    const DescriptorEntry* best_generic = nullptr; // best generic match
    float best_ctx_score = -1.0f;
    float best_generic_score = -1.0f;

    for (const auto& entry : dataset) {
        float score = 0.0f;

        if (use_embedding) {
            // Phase 9B: embedding is served from embeddingCache_ on hit; we
            // only block on HTTP for the cold misses (in practice none, since
            // prewarmEmbeddings() ran from the constructor).
            auto kw_emb = fetch_embedding(std::string(entry.keyword));
            if (kw_emb.size() == query_emb.size())
                score = cosine(query_emb, kw_emb);
            else
                score = word_overlap_score(descriptor, entry.keyword);
        } else {
            score = word_overlap_score(descriptor, entry.keyword);
        }

        if (score < cfg_.similarity_threshold)
            continue;

        if (entry.context == ctx && score > best_ctx_score) {
            best_ctx_score = score;
            best_ctx = &entry;
        } else if (entry.context == SoundContext::Generic && score > best_generic_score) {
            best_generic_score = score;
            best_generic = &entry;
        }
    }

    // Prefer context-specific match when its score is competitive with the
    // generic match; otherwise fall back to the higher-scoring generic entry.
    // This avoids fuzzy-matching a query like "pad" to "plucky"/Bass when an
    // exact-match "pad"/Generic entry exists.
    if (best_ctx && best_ctx_score + 0.05f >= best_generic_score)
        return best_ctx;
    if (best_generic)
        return best_generic;
    if (best_ctx)
        return best_ctx;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// apply — main entry point (searches custom entries first, then static dataset)
// ---------------------------------------------------------------------------

int SemanticMapper::apply(const std::string& prompt, PatchStruct& patch) const {
    const SoundContext ctx = infer_context(prompt);
    const auto tokens = tokenise(prompt);
    int matched = 0;

    for (const auto& tok : tokens) {
        // Custom entries take priority over the static dataset.
        if (!customEntries_.empty()) {
            const CustomEntry* best_ctx = nullptr;
            const CustomEntry* best_gen = nullptr;
            float best_ctx_sc = -1.0f, best_gen_sc = -1.0f;

            for (const auto& ce : customEntries_) {
                const float sc = word_overlap_score(tok, ce.keyword);
                if (sc < cfg_.similarity_threshold)
                    continue;
                if (ce.context == ctx && sc > best_ctx_sc) {
                    best_ctx_sc = sc;
                    best_ctx = &ce;
                } else if (ce.context == SoundContext::Generic && sc > best_gen_sc) {
                    best_gen_sc = sc;
                    best_gen = &ce;
                }
            }

            const CustomEntry* chosen = best_ctx ? best_ctx : best_gen;
            if (chosen) {
                apply_delta(patch, chosen->delta);
                ++matched;
                continue;
            }
        }

        // Fall through to compiled-in static dataset.
        auto entry = best_match(tok, ctx);
        if (!entry)
            continue;
        apply_delta(patch, (*entry)->delta);
        ++matched;
    }
    return matched;
}

// ---------------------------------------------------------------------------
// Phase 9B: JSON serialisation helpers built on juce::JSON / juce::var.
// Replaces the hand-rolled substring scans that lived here originally
// (engineering audit flagged the same pattern AgentBridge already migrated
// away from in commit 91d5e4b).
// ---------------------------------------------------------------------------

namespace {

// juce::var lookups never throw and treat missing keys as void; these
// helpers normalise the access pattern across the loader / parser.
bool varHasKey(const juce::var& v, const char* key) {
    if (auto* obj = v.getDynamicObject())
        return obj->hasProperty(juce::Identifier{key});
    return false;
}

std::string varStr(const juce::var& v, const char* key) {
    if (auto* obj = v.getDynamicObject())
        return obj->getProperty(juce::Identifier{key}).toString().toStdString();
    return {};
}

float varFloat(const juce::var& v, const char* key) {
    if (auto* obj = v.getDynamicObject())
        return static_cast<float>(static_cast<double>(obj->getProperty(juce::Identifier{key})));
    return 0.0f;
}

const char* contextToStr(SoundContext c) {
    switch (c) {
    case SoundContext::Bass:
        return "Bass";
    case SoundContext::Pad:
        return "Pad";
    case SoundContext::Lead:
        return "Lead";
    case SoundContext::Keys:
        return "Keys";
    case SoundContext::Percussion:
        return "Percussion";
    case SoundContext::Arp:
        return "Arp";
    case SoundContext::Texture:
        return "Texture";
    default:
        return "Generic";
    }
}

SoundContext contextFromStr(const std::string& s) {
    if (s == "Bass")
        return SoundContext::Bass;
    if (s == "Pad")
        return SoundContext::Pad;
    if (s == "Lead")
        return SoundContext::Lead;
    if (s == "Keys")
        return SoundContext::Keys;
    if (s == "Percussion")
        return SoundContext::Percussion;
    if (s == "Arp")
        return SoundContext::Arp;
    if (s == "Texture")
        return SoundContext::Texture;
    return SoundContext::Generic;
}

const char* oscTypeToStr(OscType t) {
    switch (t) {
    case OscType::Sawtooth:
        return "Sawtooth";
    case OscType::Square:
        return "Square";
    case OscType::Triangle:
        return "Triangle";
    case OscType::Noise:
        return "Noise";
    case OscType::FM:
        return "FM";
    default:
        return "Sine";
    }
}

OscType oscTypeFromStr(const std::string& s) {
    if (s == "Sawtooth")
        return OscType::Sawtooth;
    if (s == "Square")
        return OscType::Square;
    if (s == "Triangle")
        return OscType::Triangle;
    if (s == "Noise")
        return OscType::Noise;
    if (s == "FM")
        return OscType::FM;
    return OscType::Sine;
}

const char* lfoTargetToStr(LfoTarget t) {
    if (t == LfoTarget::Amplitude)
        return "Amplitude";
    if (t == LfoTarget::Pitch)
        return "Pitch";
    return "FilterCutoff";
}

LfoTarget lfoTargetFromStr(const std::string& s) {
    if (s == "Amplitude")
        return LfoTarget::Amplitude;
    if (s == "Pitch")
        return LfoTarget::Pitch;
    return LfoTarget::FilterCutoff;
}

// deltaToVar — serialise a PatchDelta to a juce::var DynamicObject. Only
// fields with a value are emitted (preserves the round-trip semantics of
// the previous std::ostringstream implementation).
juce::var deltaToVar(const PatchDelta& d) {
    auto* obj = new juce::DynamicObject{};

#define EMIT_F(name, member)                                                                                           \
    if (d.member)                                                                                                      \
        obj->setProperty(juce::Identifier{#name}, static_cast<double>(*d.member));

    EMIT_F(osc0_volume, osc0_volume)
    EMIT_F(osc0_semitone, osc0_semitone)
    EMIT_F(osc0_detune, osc0_detune)
    EMIT_F(osc0_fm_ratio, osc0_fm_ratio)
    EMIT_F(osc0_fm_depth, osc0_fm_depth)
    EMIT_F(osc0_pulse_width, osc0_pulse_width)
    EMIT_F(osc1_volume, osc1_volume)
    EMIT_F(osc1_detune, osc1_detune)
    EMIT_F(filter_cutoff, filter_cutoff)
    EMIT_F(filter_resonance, filter_resonance)
    EMIT_F(filter_drive, filter_drive)
    EMIT_F(filter_env_mod, filter_env_mod)
    EMIT_F(amp_attack, amp_attack)
    EMIT_F(amp_decay, amp_decay)
    EMIT_F(amp_sustain, amp_sustain)
    EMIT_F(amp_release, amp_release)
    EMIT_F(flt_attack, flt_attack)
    EMIT_F(flt_decay, flt_decay)
    EMIT_F(flt_sustain, flt_sustain)
    EMIT_F(flt_release, flt_release)
    EMIT_F(lfo0_rate, lfo0_rate)
    EMIT_F(lfo0_depth, lfo0_depth)
    EMIT_F(reverb_size, reverb_size)
    EMIT_F(reverb_damping, reverb_damping)
    EMIT_F(reverb_width, reverb_width)
    EMIT_F(reverb_mix, reverb_mix)
    EMIT_F(delay_time, delay_time)
    EMIT_F(delay_feedback, delay_feedback)
    EMIT_F(delay_mix, delay_mix)
    EMIT_F(master_gain, master_gain)
    EMIT_F(portamento, portamento)

#undef EMIT_F

    if (d.osc0_type)
        obj->setProperty("osc0_type", juce::String(oscTypeToStr(*d.osc0_type)));
    if (d.filter_type)
        obj->setProperty("filter_type", juce::String("LowPass")); // conservative: only LowPass confirmed in dataset
    if (d.lfo0_waveform)
        obj->setProperty("lfo0_waveform", juce::String((*d.lfo0_waveform == LfoWaveform::Square) ? "Square" : "Sine"));
    if (d.lfo0_target)
        obj->setProperty("lfo0_target", juce::String(lfoTargetToStr(*d.lfo0_target)));
    if (d.osc1_enabled)
        obj->setProperty("osc1_enabled", *d.osc1_enabled);
    if (d.voice_count)
        obj->setProperty("voice_count", static_cast<int>(*d.voice_count));

    return juce::var{obj};
}

PatchDelta parseDeltaFromVar(const juce::var& v) {
    PatchDelta d;

    auto getF = [&](const char* k) -> std::optional<float> {
        if (!varHasKey(v, k))
            return std::nullopt;
        return varFloat(v, k);
    };

    d.osc0_volume = getF("osc0_volume");
    d.osc0_semitone = getF("osc0_semitone");
    d.osc0_detune = getF("osc0_detune");
    d.osc0_fm_ratio = getF("osc0_fm_ratio");
    d.osc0_fm_depth = getF("osc0_fm_depth");
    d.osc0_pulse_width = getF("osc0_pulse_width");
    d.osc1_volume = getF("osc1_volume");
    d.osc1_detune = getF("osc1_detune");
    d.filter_cutoff = getF("filter_cutoff");
    d.filter_resonance = getF("filter_resonance");
    d.filter_drive = getF("filter_drive");
    d.filter_env_mod = getF("filter_env_mod");
    d.amp_attack = getF("amp_attack");
    d.amp_decay = getF("amp_decay");
    d.amp_sustain = getF("amp_sustain");
    d.amp_release = getF("amp_release");
    d.flt_attack = getF("flt_attack");
    d.flt_decay = getF("flt_decay");
    d.flt_sustain = getF("flt_sustain");
    d.flt_release = getF("flt_release");
    d.lfo0_rate = getF("lfo0_rate");
    d.lfo0_depth = getF("lfo0_depth");
    d.reverb_size = getF("reverb_size");
    d.reverb_damping = getF("reverb_damping");
    d.reverb_width = getF("reverb_width");
    d.reverb_mix = getF("reverb_mix");
    d.delay_time = getF("delay_time");
    d.delay_feedback = getF("delay_feedback");
    d.delay_mix = getF("delay_mix");
    d.master_gain = getF("master_gain");
    d.portamento = getF("portamento");

    const auto ost = varStr(v, "osc0_type");
    if (!ost.empty())
        d.osc0_type = oscTypeFromStr(ost);

    if (varHasKey(v, "filter_type"))
        d.filter_type = FilterType::LowPass;

    if (varHasKey(v, "lfo0_waveform")) {
        const auto lw = varStr(v, "lfo0_waveform");
        d.lfo0_waveform = (lw == "Square") ? LfoWaveform::Square : LfoWaveform::Sine;
    }

    const auto lt = varStr(v, "lfo0_target");
    if (!lt.empty())
        d.lfo0_target = lfoTargetFromStr(lt);

    if (varHasKey(v, "osc1_enabled")) {
        if (auto* obj = v.getDynamicObject())
            d.osc1_enabled = static_cast<bool>(obj->getProperty("osc1_enabled"));
    }

    if (varHasKey(v, "voice_count")) {
        if (auto* obj = v.getDynamicObject())
            d.voice_count = static_cast<uint8_t>(static_cast<int>(obj->getProperty("voice_count")));
    }

    return d;
}

// Walk a juce::var that is either an array of entries or an object with an
// "entries" array, populating `out`. Static (readonly=true) entries are
// skipped — the loader only owns user-defined customs.
void parseCustomEntriesFromVar(const juce::var& root, std::vector<CustomEntry>& out) {
    // Accept either a bare array of entries or {"entries": [...]} (the shape
    // emitted by AgentBridge::buildDictionarySaveFrame).
    juce::var arrVar = root;
    if (auto* rootObj = root.getDynamicObject()) {
        if (rootObj->hasProperty("entries"))
            arrVar = rootObj->getProperty("entries");
    }
    auto* arr = arrVar.getArray();
    if (arr == nullptr)
        return;

    for (const auto& entryVar : *arr) {
        auto* entryObj = entryVar.getDynamicObject();
        if (entryObj == nullptr)
            continue;
        if (static_cast<bool>(entryObj->getProperty("readonly")))
            continue;

        CustomEntry e;
        e.keyword = entryObj->getProperty("keyword").toString().toStdString();
        e.context = contextFromStr(entryObj->getProperty("context").toString().toStdString());

        const auto deltaVar = entryObj->getProperty("delta");
        if (deltaVar.getDynamicObject() != nullptr)
            e.delta = parseDeltaFromVar(deltaVar);

        if (!e.keyword.empty())
            out.push_back(std::move(e));
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Issue #90: custom entry management
// ---------------------------------------------------------------------------

void SemanticMapper::loadCustomEntries(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f)
        return;
    const std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    customEntries_.clear();

    const auto parsed = juce::JSON::parse(juce::String(json));
    if (parsed.isVoid())
        return;
    parseCustomEntriesFromVar(parsed, customEntries_);
}

void SemanticMapper::parseAndSaveCustomEntries(const std::string& json, const std::string& json_path) {
    // Frame shape from AgentBridge::buildDictionarySaveFrame:
    //   {"type":"save_dictionary","entries":[ {...}, ... ]}
    // We accept either the wrapped object or a bare array for robustness.
    const auto parsed = juce::JSON::parse(juce::String(json));
    if (parsed.isVoid())
        return;

    customEntries_.clear();
    parseCustomEntriesFromVar(parsed, customEntries_);

    if (json_path.empty())
        return;

    juce::Array<juce::var> outArr;
    outArr.ensureStorageAllocated(static_cast<int>(customEntries_.size()));
    for (const auto& ce : customEntries_) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("keyword", juce::String(ce.keyword));
        obj->setProperty("context", juce::String(contextToStr(ce.context)));
        obj->setProperty("delta", deltaToVar(ce.delta));
        outArr.add(juce::var{obj});
    }

    std::ofstream out(json_path, std::ios::out | std::ios::trunc);
    if (!out)
        return;
    out << juce::JSON::toString(juce::var{outArr}, /*allOnOneLine*/ true).toStdString();
}

void SemanticMapper::addCustomEntry(CustomEntry e) { customEntries_.push_back(std::move(e)); }

std::string SemanticMapper::dumpAllToJson() const {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(get_descriptor_dataset().size() + customEntries_.size()));

    // Static dataset (readonly).
    for (const auto& e : get_descriptor_dataset()) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("keyword", juce::String(std::string(e.keyword)));
        obj->setProperty("context", juce::String(contextToStr(e.context)));
        obj->setProperty("readonly", true);
        obj->setProperty("delta", deltaToVar(e.delta));
        arr.add(juce::var{obj});
    }

    // User-defined custom entries (editable).
    for (const auto& e : customEntries_) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("keyword", juce::String(e.keyword));
        obj->setProperty("context", juce::String(contextToStr(e.context)));
        obj->setProperty("readonly", false);
        obj->setProperty("delta", deltaToVar(e.delta));
        arr.add(juce::var{obj});
    }

    return juce::JSON::toString(juce::var{arr}, /*allOnOneLine*/ true).toStdString();
}

} // namespace agentic_synth::mapper
