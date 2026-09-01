# ── Wasm module (#306) ──────────────────────────────────────────────────────
# Browser/demo glue around agentic_synth_capi. MODULARIZE + EXPORT_ES6 so
# the web app can `import createAgsynthModule` without a global; memory
# growth so offline renders are not capped at the initial heap.
#
# No pthreads: SharedArrayBuffer needs COOP/COEP headers and the demo
# runs on a static host that does not send them.

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/wasm")

add_executable(agsynth agsynth_bindings.cpp)
target_link_libraries(agsynth PRIVATE agentic_synth_capi)
agentic_synth_configure_target(agsynth)

set_target_properties(agsynth PROPERTIES
    OUTPUT_NAME "agsynth"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/wasm"
)

target_link_options(agsynth PRIVATE
    "SHELL:-sMODULARIZE=1"
    "SHELL:-sEXPORT_NAME=createAgsynthModule"
    "SHELL:-sENVIRONMENT=web,worker"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sEXPORT_ES6=1"
    "SHELL:-sEXPORTED_FUNCTIONS=_ags_engine_create,_ags_engine_destroy,_ags_engine_set_patch,_ags_engine_set_param,_ags_engine_push_events,_ags_engine_render,_ags_patch_struct_size,_ags_render_offline,_malloc,_free"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,HEAPU32"
)
