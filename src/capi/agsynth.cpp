#include "agsynth.h"

#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/RealtimeSafety.h"
#include "engine/VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace {

using agentic_synth::kMaxLfos;
using agentic_synth::kMaxOscillators;
using agentic_synth::kPatchStructVersion;
using agentic_synth::make_default_patch;
using agentic_synth::patch_is_finite;
using agentic_synth::PatchStruct;
using agentic_synth::validate_patch;
using agentic_synth::engine::ScopedRealtimeContext;
using agentic_synth::engine::VoiceManager;

constexpr uint32_t kMaxQueuedEvents = 64;
constexpr int kMinBlock = 16;
constexpr int kMaxBlock = 8192;
constexpr double kMinSampleRate = 8000.0;
constexpr double kMaxSampleRate = 192000.0;

struct Engine {
    double sample_rate{44100.0};
    uint32_t max_block{512};
    VoiceManager vm{VoiceManager::kDefaultVoiceCount};
    PatchStruct patch{};
    ags_event events[kMaxQueuedEvents]{};
    uint32_t event_count{0};
    std::unique_ptr<float[]> scratch_l;
    std::unique_ptr<float[]> scratch_r;
};

int strcmp_prefix(const char* path, const char* head, const char** rest) {
    const std::size_t n = std::strlen(head);
    if (std::strncmp(path, head, n) != 0)
        return 0;
    *rest = path + n;
    return 1;
}

int parse_index(const char** p, int max_exclusive) {
    if (**p < '0' || **p > '9')
        return -1;
    int v = 0;
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        ++*p;
    }
    if (v < 0 || v >= max_exclusive)
        return -1;
    return v;
}

int set_enum_u8(uint8_t& dst, float value, uint8_t max_inclusive) {
    if (!std::isfinite(value))
        return AGS_ERR_PARAM;
    const int iv = static_cast<int>(std::lround(value));
    if (iv < 0 || iv > static_cast<int>(max_inclusive))
        return AGS_ERR_PARAM;
    dst = static_cast<uint8_t>(iv);
    return AGS_OK;
}

template <typename Enum> int set_enum(Enum& dst, float value, uint8_t max_inclusive) {
    uint8_t tmp = static_cast<uint8_t>(dst);
    const int rc = set_enum_u8(tmp, value, max_inclusive);
    if (rc != AGS_OK)
        return rc;
    dst = static_cast<Enum>(tmp);
    return AGS_OK;
}

int set_float(float& dst, float value) {
    if (!std::isfinite(value))
        return AGS_ERR_PARAM;
    dst = value;
    return AGS_OK;
}

// Dotted paths match libs/engine-bridge/src/lib/paramMap.ts plus PatchStruct extras.
int set_path(PatchStruct& p, const char* path, float value) {
    if (path == nullptr || path[0] == '\0')
        return AGS_ERR_PARAM;
    if (!std::isfinite(value))
        return AGS_ERR_PARAM;

    const char* rest = nullptr;
    if (strcmp_prefix(path, "osc.", &rest)) {
        int idx = parse_index(&rest, kMaxOscillators);
        if (idx < 0 || *rest != '.')
            return AGS_ERR_PARAM;
        ++rest;
        auto& o = p.osc[idx];
        if (std::strcmp(rest, "type") == 0)
            return set_enum(o.type, value, 7);
        if (std::strcmp(rest, "enabled") == 0)
            return set_enum_u8(o.enabled, value, 1);
        if (std::strcmp(rest, "volume") == 0)
            return set_float(o.volume, value);
        if (std::strcmp(rest, "pan") == 0)
            return set_float(o.pan, value);
        if (std::strcmp(rest, "detune_cents") == 0)
            return set_float(o.detune_cents, value);
        if (std::strcmp(rest, "semitone_offset") == 0)
            return set_float(o.semitone_offset, value);
        if (std::strcmp(rest, "wavetable_pos") == 0)
            return set_float(o.wavetable_pos, value);
        if (std::strcmp(rest, "fm_ratio") == 0)
            return set_float(o.fm_ratio, value);
        if (std::strcmp(rest, "fm_depth") == 0)
            return set_float(o.fm_depth, value);
        if (std::strcmp(rest, "pulse_width") == 0)
            return set_float(o.pulse_width, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "lfo.", &rest)) {
        int idx = parse_index(&rest, kMaxLfos);
        if (idx < 0 || *rest != '.')
            return AGS_ERR_PARAM;
        ++rest;
        auto& l = p.lfo[idx];
        if (std::strcmp(rest, "waveform") == 0)
            return set_enum(l.waveform, value, 4);
        if (std::strcmp(rest, "target") == 0)
            return set_enum(l.target, value, 6);
        if (std::strcmp(rest, "bpm_sync") == 0)
            return set_enum_u8(l.bpm_sync, value, 1);
        if (std::strcmp(rest, "rate_hz") == 0)
            return set_float(l.rate_hz, value);
        if (std::strcmp(rest, "depth") == 0)
            return set_float(l.depth, value);
        if (std::strcmp(rest, "phase_offset") == 0)
            return set_float(l.phase_offset, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "filter.", &rest)) {
        if (std::strcmp(rest, "type") == 0)
            return set_enum(p.filter.type, value, 4);
        if (std::strcmp(rest, "cutoff_hz") == 0)
            return set_float(p.filter.cutoff_hz, value);
        if (std::strcmp(rest, "resonance") == 0)
            return set_float(p.filter.resonance, value);
        if (std::strcmp(rest, "env_mod") == 0)
            return set_float(p.filter.env_mod, value);
        if (std::strcmp(rest, "key_track") == 0)
            return set_float(p.filter.key_track, value);
        if (std::strcmp(rest, "drive") == 0)
            return set_float(p.filter.drive, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "amp_env.", &rest)) {
        if (std::strcmp(rest, "attack_s") == 0)
            return set_float(p.amp_env.attack_s, value);
        if (std::strcmp(rest, "decay_s") == 0)
            return set_float(p.amp_env.decay_s, value);
        if (std::strcmp(rest, "sustain") == 0)
            return set_float(p.amp_env.sustain, value);
        if (std::strcmp(rest, "release_s") == 0)
            return set_float(p.amp_env.release_s, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "filter_env.", &rest)) {
        if (std::strcmp(rest, "attack_s") == 0)
            return set_float(p.filter_env.attack_s, value);
        if (std::strcmp(rest, "decay_s") == 0)
            return set_float(p.filter_env.decay_s, value);
        if (std::strcmp(rest, "sustain") == 0)
            return set_float(p.filter_env.sustain, value);
        if (std::strcmp(rest, "release_s") == 0)
            return set_float(p.filter_env.release_s, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "reverb.", &rest)) {
        if (std::strcmp(rest, "size") == 0)
            return set_float(p.reverb.size, value);
        if (std::strcmp(rest, "damping") == 0)
            return set_float(p.reverb.damping, value);
        if (std::strcmp(rest, "width") == 0)
            return set_float(p.reverb.width, value);
        if (std::strcmp(rest, "mix") == 0)
            return set_float(p.reverb.mix, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "delay.", &rest)) {
        if (std::strcmp(rest, "time_s") == 0)
            return set_float(p.delay.time_s, value);
        if (std::strcmp(rest, "feedback") == 0)
            return set_float(p.delay.feedback, value);
        if (std::strcmp(rest, "mix") == 0)
            return set_float(p.delay.mix, value);
        if (std::strcmp(rest, "stereo") == 0)
            return set_float(p.delay.stereo, value);
        if (std::strcmp(rest, "bpm_sync") == 0)
            return set_enum_u8(p.delay.bpm_sync, value, 1);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "chorus.", &rest)) {
        if (std::strcmp(rest, "rate_hz") == 0)
            return set_float(p.chorus.rate_hz, value);
        if (std::strcmp(rest, "depth") == 0)
            return set_float(p.chorus.depth, value);
        if (std::strcmp(rest, "mix") == 0)
            return set_float(p.chorus.mix, value);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "tubesat.", &rest)) {
        if (std::strcmp(rest, "drive") == 0)
            return set_float(p.tubesat.drive, value);
        if (std::strcmp(rest, "mix") == 0)
            return set_float(p.tubesat.mix, value);
        return AGS_ERR_PARAM;
    }
    if (std::strcmp(path, "master_gain") == 0)
        return set_float(p.master_gain, value);
    if (std::strcmp(path, "portamento_s") == 0)
        return set_float(p.portamento_s, value);
    if (std::strcmp(path, "voice_count") == 0) {
        if (!std::isfinite(value))
            return AGS_ERR_PARAM;
        const int iv = static_cast<int>(std::lround(value));
        if (iv < 1 || iv > 32)
            return AGS_ERR_PARAM;
        p.voice_count = static_cast<uint8_t>(iv);
        return AGS_OK;
    }
    if (std::strcmp(path, "reverb_send_hpf_hz") == 0)
        return set_float(p.reverb_send_hpf_hz, value);
    return AGS_ERR_PARAM;
}

int get_path(const PatchStruct& p, const char* path, float* out) {
    if (path == nullptr || out == nullptr)
        return AGS_ERR_NULL;
    const char* rest = nullptr;
    auto ok = [&](float v) {
        *out = v;
        return AGS_OK;
    };
    if (strcmp_prefix(path, "osc.", &rest)) {
        int idx = parse_index(&rest, kMaxOscillators);
        if (idx < 0 || *rest != '.')
            return AGS_ERR_PARAM;
        ++rest;
        const auto& o = p.osc[idx];
        if (std::strcmp(rest, "type") == 0)
            return ok(static_cast<float>(static_cast<uint8_t>(o.type)));
        if (std::strcmp(rest, "enabled") == 0)
            return ok(static_cast<float>(o.enabled));
        if (std::strcmp(rest, "volume") == 0)
            return ok(o.volume);
        if (std::strcmp(rest, "pan") == 0)
            return ok(o.pan);
        if (std::strcmp(rest, "detune_cents") == 0)
            return ok(o.detune_cents);
        if (std::strcmp(rest, "semitone_offset") == 0)
            return ok(o.semitone_offset);
        if (std::strcmp(rest, "wavetable_pos") == 0)
            return ok(o.wavetable_pos);
        if (std::strcmp(rest, "fm_ratio") == 0)
            return ok(o.fm_ratio);
        if (std::strcmp(rest, "fm_depth") == 0)
            return ok(o.fm_depth);
        if (std::strcmp(rest, "pulse_width") == 0)
            return ok(o.pulse_width);
        return AGS_ERR_PARAM;
    }
    if (strcmp_prefix(path, "lfo.", &rest)) {
        int idx = parse_index(&rest, kMaxLfos);
        if (idx < 0 || *rest != '.')
            return AGS_ERR_PARAM;
        ++rest;
        const auto& l = p.lfo[idx];
        if (std::strcmp(rest, "waveform") == 0)
            return ok(static_cast<float>(static_cast<uint8_t>(l.waveform)));
        if (std::strcmp(rest, "target") == 0)
            return ok(static_cast<float>(static_cast<uint8_t>(l.target)));
        if (std::strcmp(rest, "bpm_sync") == 0)
            return ok(static_cast<float>(l.bpm_sync));
        if (std::strcmp(rest, "rate_hz") == 0)
            return ok(l.rate_hz);
        if (std::strcmp(rest, "depth") == 0)
            return ok(l.depth);
        if (std::strcmp(rest, "phase_offset") == 0)
            return ok(l.phase_offset);
        return AGS_ERR_PARAM;
    }
    if (std::strcmp(path, "filter.cutoff_hz") == 0)
        return ok(p.filter.cutoff_hz);
    if (std::strcmp(path, "filter.resonance") == 0)
        return ok(p.filter.resonance);
    if (std::strcmp(path, "filter.env_mod") == 0)
        return ok(p.filter.env_mod);
    if (std::strcmp(path, "filter.key_track") == 0)
        return ok(p.filter.key_track);
    if (std::strcmp(path, "filter.drive") == 0)
        return ok(p.filter.drive);
    if (std::strcmp(path, "filter.type") == 0)
        return ok(static_cast<float>(static_cast<uint8_t>(p.filter.type)));
    if (std::strcmp(path, "amp_env.attack_s") == 0)
        return ok(p.amp_env.attack_s);
    if (std::strcmp(path, "amp_env.decay_s") == 0)
        return ok(p.amp_env.decay_s);
    if (std::strcmp(path, "amp_env.sustain") == 0)
        return ok(p.amp_env.sustain);
    if (std::strcmp(path, "amp_env.release_s") == 0)
        return ok(p.amp_env.release_s);
    if (std::strcmp(path, "filter_env.attack_s") == 0)
        return ok(p.filter_env.attack_s);
    if (std::strcmp(path, "filter_env.decay_s") == 0)
        return ok(p.filter_env.decay_s);
    if (std::strcmp(path, "filter_env.sustain") == 0)
        return ok(p.filter_env.sustain);
    if (std::strcmp(path, "filter_env.release_s") == 0)
        return ok(p.filter_env.release_s);
    if (std::strcmp(path, "reverb.size") == 0)
        return ok(p.reverb.size);
    if (std::strcmp(path, "reverb.damping") == 0)
        return ok(p.reverb.damping);
    if (std::strcmp(path, "reverb.width") == 0)
        return ok(p.reverb.width);
    if (std::strcmp(path, "reverb.mix") == 0)
        return ok(p.reverb.mix);
    if (std::strcmp(path, "delay.time_s") == 0)
        return ok(p.delay.time_s);
    if (std::strcmp(path, "delay.feedback") == 0)
        return ok(p.delay.feedback);
    if (std::strcmp(path, "delay.mix") == 0)
        return ok(p.delay.mix);
    if (std::strcmp(path, "delay.stereo") == 0)
        return ok(p.delay.stereo);
    if (std::strcmp(path, "delay.bpm_sync") == 0)
        return ok(static_cast<float>(p.delay.bpm_sync));
    if (std::strcmp(path, "master_gain") == 0)
        return ok(p.master_gain);
    if (std::strcmp(path, "portamento_s") == 0)
        return ok(p.portamento_s);
    if (std::strcmp(path, "voice_count") == 0)
        return ok(static_cast<float>(p.voice_count));
    if (std::strcmp(path, "reverb_send_hpf_hz") == 0)
        return ok(p.reverb_send_hpf_hz);
    if (std::strcmp(path, "chorus.rate_hz") == 0)
        return ok(p.chorus.rate_hz);
    if (std::strcmp(path, "chorus.depth") == 0)
        return ok(p.chorus.depth);
    if (std::strcmp(path, "chorus.mix") == 0)
        return ok(p.chorus.mix);
    if (std::strcmp(path, "tubesat.drive") == 0)
        return ok(p.tubesat.drive);
    if (std::strcmp(path, "tubesat.mix") == 0)
        return ok(p.tubesat.mix);
    return AGS_ERR_PARAM;
}

void apply_event(Engine& e, const ags_event& ev) {
    switch (ev.kind) {
    case AGS_EVENT_NOTE_ON: {
        const float vel = static_cast<float>(ev.velocity) / 127.0f;
        e.vm.noteOn(static_cast<int>(ev.note), std::clamp(vel, 0.0f, 1.0f));
        break;
    }
    case AGS_EVENT_NOTE_OFF:
        e.vm.noteOff(static_cast<int>(ev.note));
        break;
    case AGS_EVENT_CC:
        // CC mapping lives in MidiHandler; ignored here (safe no-op).
        break;
    default:
        break;
    }
}

void sort_events(ags_event* events, uint32_t n) {
    // Insertion sort — no heap, N is small.
    for (uint32_t i = 1; i < n; ++i) {
        ags_event key = events[i];
        uint32_t j = i;
        while (j > 0 && events[j - 1].sample_offset > key.sample_offset) {
            events[j] = events[j - 1];
            --j;
        }
        events[j] = key;
    }
}

int render_into(Engine& e, float* out, uint32_t frames, uint32_t channels) {
    if (out == nullptr)
        return AGS_ERR_NULL;
    if (channels != 1 && channels != 2)
        return AGS_ERR_PARAM;
    if (frames == 0)
        return AGS_OK;
    if (frames > e.max_block)
        return AGS_ERR_SIZE;

    sort_events(e.events, e.event_count);

    ScopedRealtimeContext rt;
    uint32_t i = 0;
    uint32_t ev = 0;
    while (i < frames) {
        while (ev < e.event_count && e.events[ev].sample_offset == i) {
            apply_event(e, e.events[ev]);
            ++ev;
        }
        uint32_t next = frames;
        if (ev < e.event_count && e.events[ev].sample_offset < frames)
            next = e.events[ev].sample_offset;
        const uint32_t n = next - i;
        e.vm.renderBlock(e.scratch_l.get(), e.scratch_r.get(), static_cast<int>(n));
        for (uint32_t s = 0; s < n; ++s) {
            const float l = e.scratch_l[s];
            const float r = e.scratch_r[s];
            if (channels == 2) {
                out[(i + s) * 2u] = l;
                out[(i + s) * 2u + 1u] = r;
            } else {
                out[i + s] = 0.5f * (l + r);
            }
        }
        i = next;
    }
    e.event_count = 0;
    return AGS_OK;
}

// Map absolute sample offsets onto one live block. render_into treats
// sample_offset as block-relative and then clears the queue, so callers of
// ags_engine_render must not push times ≥ the upcoming block length.
int push_abs_events_for_chunk(ags_engine* eng, const ags_event* events, uint32_t event_count, uint32_t done,
                              uint32_t n) {
    if (event_count == 0)
        return AGS_OK;
    ags_event chunk[kMaxQueuedEvents];
    uint32_t chunk_n = 0;
    const uint32_t end = done + n; // half-open [done, done+n)
    for (uint32_t i = 0; i < event_count; ++i) {
        const uint32_t off = events[i].sample_offset;
        if (off < done || off >= end)
            continue;
        if (chunk_n >= kMaxQueuedEvents)
            return AGS_ERR_SIZE;
        chunk[chunk_n] = events[i];
        chunk[chunk_n].sample_offset = off - done;
        ++chunk_n;
    }
    if (chunk_n == 0)
        return AGS_OK;
    return ags_engine_push_events(eng, chunk, chunk_n);
}

int copy_patch(Engine& e, const void* bytes, uint32_t len) {
    if (bytes == nullptr)
        return AGS_ERR_NULL;
    if (len != sizeof(PatchStruct))
        return AGS_ERR_SIZE;
    PatchStruct incoming{};
    std::memcpy(&incoming, bytes, sizeof(incoming));
    if (incoming.version != kPatchStructVersion)
        return AGS_ERR_PARAM;
    if (!patch_is_finite(incoming))
        return AGS_ERR_PARAM;
    e.patch = validate_patch(incoming);
    e.vm.applyPatch(e.patch);
    e.vm.primeSmoothers();
    return AGS_OK;
}

} // namespace

extern "C" {

uint32_t ags_patch_struct_size(void) { return static_cast<uint32_t>(sizeof(PatchStruct)); }

ags_engine* ags_engine_create(double sample_rate, int max_block) {
    if (!(sample_rate >= kMinSampleRate && sample_rate <= kMaxSampleRate))
        return nullptr;
    if (max_block < kMinBlock || max_block > kMaxBlock)
        return nullptr;
    try {
        auto* e = new Engine();
        e->sample_rate = sample_rate;
        e->max_block = static_cast<uint32_t>(max_block);
        e->scratch_l = std::make_unique<float[]>(e->max_block);
        e->scratch_r = std::make_unique<float[]>(e->max_block);
        e->patch = make_default_patch();
        e->vm.prepare(sample_rate);
        e->vm.applyPatch(e->patch);
        e->vm.primeSmoothers();
        return reinterpret_cast<ags_engine*>(e);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void ags_engine_destroy(ags_engine* engine) { delete reinterpret_cast<Engine*>(engine); }

int ags_engine_set_patch(ags_engine* engine, const void* patch_struct_bytes, uint32_t len) {
    if (engine == nullptr)
        return AGS_ERR_NULL;
    return copy_patch(*reinterpret_cast<Engine*>(engine), patch_struct_bytes, len);
}

int ags_engine_set_param(ags_engine* engine, const char* path, float value) {
    if (engine == nullptr)
        return AGS_ERR_NULL;
    auto& e = *reinterpret_cast<Engine*>(engine);
    const int rc = set_path(e.patch, path, value);
    if (rc != AGS_OK)
        return rc;
    e.patch = validate_patch(e.patch);
    e.vm.applyPatch(e.patch);
    return AGS_OK;
}

int ags_engine_get_param(const ags_engine* engine, const char* path, float* out) {
    if (engine == nullptr)
        return AGS_ERR_NULL;
    return get_path(reinterpret_cast<const Engine*>(engine)->patch, path, out);
}

int ags_engine_push_events(ags_engine* engine, const ags_event* events, uint32_t count) {
    if (engine == nullptr)
        return AGS_ERR_NULL;
    if (count == 0)
        return AGS_OK;
    if (events == nullptr)
        return AGS_ERR_NULL;
    auto& e = *reinterpret_cast<Engine*>(engine);
    if (e.event_count + count > kMaxQueuedEvents)
        return AGS_ERR_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        const ags_event& ev = events[i];
        if (ev.kind != AGS_EVENT_NOTE_ON && ev.kind != AGS_EVENT_NOTE_OFF && ev.kind != AGS_EVENT_CC)
            return AGS_ERR_PARAM;
        e.events[e.event_count++] = ev;
    }
    return AGS_OK;
}

int ags_engine_render(ags_engine* engine, float* out_interleaved, uint32_t frames, uint32_t channels) {
    if (engine == nullptr)
        return AGS_ERR_NULL;
    return render_into(*reinterpret_cast<Engine*>(engine), out_interleaved, frames, channels);
}

int ags_render_offline(const void* patch_bytes, uint32_t patch_len, const ags_event* events, uint32_t event_count,
                       double sample_rate, uint32_t frames, float* out_interleaved) {
    if (out_interleaved == nullptr)
        return AGS_ERR_NULL;
    if (frames == 0)
        return AGS_OK;
    const int block = static_cast<int>(std::min<uint32_t>(frames, static_cast<uint32_t>(kMaxBlock)));
    ags_engine* eng = ags_engine_create(sample_rate, std::max(block, kMinBlock));
    if (eng == nullptr)
        return AGS_ERR_PARAM;
    int rc = ags_engine_set_patch(eng, patch_bytes, patch_len);
    if (rc != AGS_OK) {
        ags_engine_destroy(eng);
        return rc;
    }
    if (event_count > 0 && events == nullptr) {
        ags_engine_destroy(eng);
        return AGS_ERR_NULL;
    }
    auto* e = reinterpret_cast<Engine*>(eng);
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min(frames - done, e->max_block);
        rc = push_abs_events_for_chunk(eng, events, event_count, done, n);
        if (rc != AGS_OK) {
            ags_engine_destroy(eng);
            return rc;
        }
        rc = ags_engine_render(eng, out_interleaved + done * 2u, n, 2);
        if (rc != AGS_OK) {
            ags_engine_destroy(eng);
            return rc;
        }
        done += n;
    }
    ags_engine_destroy(eng);
    return AGS_OK;
}

int ags_state_size(const ags_engine* engine, uint32_t* len) {
    if (engine == nullptr || len == nullptr)
        return AGS_ERR_NULL;
    *len = static_cast<uint32_t>(sizeof(PatchStruct));
    return AGS_OK;
}

int ags_state_save(const ags_engine* engine, void* buf, uint32_t len) {
    if (engine == nullptr || buf == nullptr)
        return AGS_ERR_NULL;
    if (len < sizeof(PatchStruct))
        return AGS_ERR_SIZE;
    std::memcpy(buf, &reinterpret_cast<const Engine*>(engine)->patch, sizeof(PatchStruct));
    return AGS_OK;
}

int ags_state_load(ags_engine* engine, const void* buf, uint32_t len) { return ags_engine_set_patch(engine, buf, len); }

} // extern "C"
