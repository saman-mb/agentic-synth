// agsynth.h — stable C API. Semver'd; breaking changes bump major version.
// RFC: docs/rfcs/cpp-dsp-core.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ags_engine ags_engine;

enum { AGS_OK = 0, AGS_ERR_PARAM = 1, AGS_ERR_SIZE = 2, AGS_ERR_STATE = 3, AGS_ERR_NULL = 4 };

enum { AGS_EVENT_NOTE_ON = 1, AGS_EVENT_NOTE_OFF = 2, AGS_EVENT_CC = 3 };

typedef struct {
    uint32_t kind; /* AGS_EVENT_NOTE_ON / OFF / CC */
    uint8_t note;
    uint8_t velocity;
    uint8_t cc;
    uint8_t _pad;
    uint32_t sample_offset;
} ags_event;

/* Byte size of the versioned PatchStruct blob accepted by set_patch. */
uint32_t ags_patch_struct_size(void);

/* Lifecycle — engine construction may allocate; render may not. */
ags_engine* ags_engine_create(double sample_rate, int max_block);
void ags_engine_destroy(ags_engine*);

/* Patch: apply whole POD snapshot (validated internally).
   Returns AGS_OK or AGS_ERR_PARAM (never crashes on bad input). */
int ags_engine_set_patch(ags_engine*, const void* patch_struct_bytes, uint32_t len);

/* Individual param at UI/automation rate: dotted path, e.g. "filter.cutoff_hz".
   Same path vocabulary as libs/engine-bridge paramMap. */
int ags_engine_set_param(ags_engine*, const char* path, float value);
int ags_engine_get_param(const ags_engine*, const char* path, float* out);

/* Events: sample-accurate within the next rendered block. */
int ags_engine_push_events(ags_engine*, const ags_event* events, uint32_t count);

/* Render. out is caller-owned interleaved stereo (or mono), frames long.
   Contract: no allocation, no locks, no syscalls inside this call. */
int ags_engine_render(ags_engine*, float* out_interleaved, uint32_t frames, uint32_t channels);

/* Deterministic offline render for parity tests (fresh engine per call). */
int ags_render_offline(const void* patch_bytes, uint32_t patch_len, const ags_event* events, uint32_t event_count,
                       double sample_rate, uint32_t frames, float* out_interleaved);

/* State save/restore (POD serialization — plugin state, app suspend). */
int ags_state_size(const ags_engine*, uint32_t* len);
int ags_state_save(const ags_engine*, void* buf, uint32_t len);
int ags_state_load(ags_engine*, const void* buf, uint32_t len);

#ifdef __cplusplus
}
#endif
