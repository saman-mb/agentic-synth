#include "agsynth.h"

#ifdef __EMSCRIPTEN__

extern "C" {
__attribute__((used)) void agsynth_keepalives(void) {
    void* volatile keep[] = {
        (void*)&ags_engine_create,     (void*)&ags_engine_destroy,     (void*)&ags_engine_set_patch,
        (void*)&ags_engine_set_param,  (void*)&ags_engine_push_events, (void*)&ags_engine_render,
        (void*)&ags_patch_struct_size, (void*)&ags_render_offline,     (void*)&ags_engine_get_param,
        (void*)&ags_state_size,        (void*)&ags_state_save,         (void*)&ags_state_load,
    };
    (void)keep;
}
}

#endif
