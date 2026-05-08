#include "agent/SessionMemory.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace agentic_synth::agent {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

float SessionMemory::normalizeCutoff(float hz) noexcept {
    const float kMinLog = std::log(20.0f);
    const float kMaxLog = std::log(20000.0f);
    float clamped = std::clamp(hz, 20.0f, 20000.0f);
    return (std::log(clamped) - kMinLog) / (kMaxLog - kMinLog);
}

float SessionMemory::denormalizeCutoff(float norm) noexcept {
    const float kMinLog = std::log(20.0f);
    const float kMaxLog = std::log(20000.0f);
    return std::exp(kMinLog + std::clamp(norm, 0.0f, 1.0f) * (kMaxLog - kMinLog));
}

PatchVector SessionMemory::extractVector(const PatchStruct& patch) noexcept {
    auto normalizeLog = [](float v, float lo, float hi) -> float {
        float clamped = std::clamp(v, lo, hi);
        float logLo = std::log(lo + 1e-6f);
        float logHi = std::log(hi + 1e-6f);
        return (std::log(clamped + 1e-6f) - logLo) / (logHi - logLo + 1e-12f);
    };

    PatchVector v{};
    v[0] = normalizeCutoff(patch.filter.cutoff_hz);
    v[1] = std::clamp(patch.filter.resonance, 0.0f, 1.0f);
    v[2] = normalizeLog(patch.amp_env.attack_s + 0.001f, 0.001f, 10.0f);
    v[3] = std::clamp(patch.amp_env.sustain, 0.0f, 1.0f);
    v[4] = std::clamp(patch.lfo[0].depth, 0.0f, 1.0f);
    v[5] = std::clamp(patch.reverb.mix, 0.0f, 1.0f);
    v[6] = std::clamp(patch.master_gain, 0.0f, 1.0f);
    v[7] = std::clamp(patch.osc[0].volume, 0.0f, 1.0f);
    return v;
}

float SessionMemory::cosineSimilarity(const PatchVector& a, const PatchVector& b) noexcept {
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    float denom = std::sqrt(normA) * std::sqrt(normB);
    return (denom < 1e-9f) ? 0.0f : dot / denom;
}

// Maps descriptive keywords to a parameter vector so that semantically similar
// prompts receive similar embeddings without requiring an external model.
PatchVector SessionMemory::promptToVector(const std::string& prompt) noexcept {
    std::string lower = prompt;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto has = [&](const char* kw) { return lower.find(kw) != std::string::npos; };

    PatchVector v{};

    // [0] filter_cutoff: dark↓ bright↑ warm-mid
    if (has("dark") || has("sub") || has("deep") || has("murky"))
        v[0] = 0.1f;
    else if (has("bright") || has("crisp") || has("sharp") || has("ice"))
        v[0] = 0.9f;
    else if (has("warm") || has("mellow") || has("soft"))
        v[0] = 0.4f;
    else
        v[0] = 0.5f;

    // [1] resonance: resonant/acid↑
    if (has("resonant") || has("acid") || has("squelch"))
        v[1] = 0.8f;
    else
        v[1] = 0.2f;

    // [2] attack: pad/slow↑ lead/pluck↓
    if (has("pad") || has("slow") || has("ambient") || has("evolving"))
        v[2] = 0.7f;
    else if (has("lead") || has("pluck") || has("stab") || has("snap"))
        v[2] = 0.05f;
    else
        v[2] = 0.3f;

    // [3] sustain: pad/sustained↑ bass/pluck↓
    if (has("pad") || has("sustained") || has("drone"))
        v[3] = 0.9f;
    else if (has("pluck") || has("stab") || has("short"))
        v[3] = 0.0f;
    else
        v[3] = 0.7f;

    // [4] lfo_depth: moving/evolving/wobble↑
    if (has("evolving") || has("moving") || has("wobble") || has("vibrato"))
        v[4] = 0.8f;
    else if (has("static") || has("still"))
        v[4] = 0.0f;
    else
        v[4] = 0.2f;

    // [5] reverb: ambient/wide/space↑ dry/bass↓
    if (has("ambient") || has("wide") || has("space") || has("reverb"))
        v[5] = 0.8f;
    else if (has("dry") || has("bass") || has("mono"))
        v[5] = 0.05f;
    else
        v[5] = 0.3f;

    // [6] master_gain: constant
    v[6] = 0.8f;

    // [7] osc0_volume: constant
    v[7] = 0.9f;

    return v;
}

// ---------------------------------------------------------------------------
// SessionMemory
// ---------------------------------------------------------------------------

void SessionMemory::recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch) {
    std::lock_guard lock(mutex_);
    events_.push_back({kind, prompt, patch});
}

std::string SessionMemory::buildRecap(const std::string& currentPrompt, int maxEvents) const {
    std::lock_guard lock(mutex_);
    if (events_.empty())
        return {};

    PatchVector queryVec = promptToVector(currentPrompt);

    // Score each event by semantic similarity to current prompt.
    struct Scored {
        float score;
        size_t idx;
    };
    std::vector<Scored> scored;
    scored.reserve(events_.size());
    for (size_t i = 0; i < events_.size(); ++i) {
        PatchVector ev = promptToVector(events_[i].prompt);
        scored.push_back({cosineSimilarity(queryVec, ev), i});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::ostringstream oss;
    oss << "Recent session feedback (most relevant first):\n";

    int count = 0;
    for (const auto& s : scored) {
        if (count >= maxEvents)
            break;
        const auto& ev = events_[s.idx];
        const char* label = (ev.kind == FeedbackKind::Like)      ? "LIKED"
                            : (ev.kind == FeedbackKind::Dislike) ? "DISLIKED"
                                                                 : "TWEAKED";
        float cutoff = ev.patch.filter.cutoff_hz;
        float attack = ev.patch.amp_env.attack_s;
        float reverb = ev.patch.reverb.mix;

        oss << "- " << label << ": \"" << ev.prompt << "\"" << " → cutoff=" << static_cast<int>(cutoff) << "Hz"
            << " attack=" << attack << "s" << " reverb=" << reverb << " (similarity=" << s.score << ")\n";
        ++count;
    }

    return oss.str();
}

PatchVector SessionMemory::computeParameterBias(const std::string& currentPrompt) const {
    std::lock_guard lock(mutex_);
    PatchVector bias{};
    if (events_.empty())
        return bias;

    PatchVector queryVec = promptToVector(currentPrompt);
    float totalWeight = 0.0f;

    for (const auto& ev : events_) {
        PatchVector ev_prompt = promptToVector(ev.prompt);
        float sim = cosineSimilarity(queryVec, ev_prompt);
        if (sim < kSimilarityThreshold)
            continue;

        // Liked events push bias toward their parameter values (+),
        // disliked events push bias away (-).
        float sign = (ev.kind == FeedbackKind::Dislike) ? -1.0f : +1.0f;
        float weight = sim * sign;

        PatchVector pv = extractVector(ev.patch);
        for (size_t i = 0; i < bias.size(); ++i) {
            // Center around 0.5: positive bias means "go higher", negative means "go lower".
            bias[i] += weight * (pv[i] - 0.5f);
        }
        totalWeight += std::abs(weight);
    }

    if (totalWeight > 1e-9f) {
        for (float& b : bias)
            b /= totalWeight;
    }

    // Clamp to [-1, +1].
    for (float& b : bias)
        b = std::clamp(b, -1.0f, 1.0f);

    return bias;
}

void SessionMemory::clear() {
    std::lock_guard lock(mutex_);
    events_.clear();
}

} // namespace agentic_synth::agent
