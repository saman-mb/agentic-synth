# JUCE-free JSI/native host around agentic_synth_capi (#308).
# PLUGIN/JUCE stay off this target. Not added under EMSCRIPTEN.

option(AGENTIC_SYNTH_HAS_JSI "Build RN JSI HostObject (requires React Native headers)" OFF)

set(AGENTIC_SYNTH_JSI_SOURCES
    host/AgsynthHost.cpp
    jsi/AgsynthHostObject.cpp
)

if(ANDROID)
    list(APPEND AGENTIC_SYNTH_JSI_SOURCES audio/AudioStreamAAudio.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND AGENTIC_SYNTH_JSI_SOURCES audio/AudioStreamRemoteIO.cpp)
else()
    list(APPEND AGENTIC_SYNTH_JSI_SOURCES audio/AudioStreamLinux.cpp)
endif()

add_library(agentic_synth_jsi STATIC ${AGENTIC_SYNTH_JSI_SOURCES})
target_include_directories(agentic_synth_jsi
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/capi"
)
target_link_libraries(agentic_synth_jsi PRIVATE agentic_synth_capi)
agentic_synth_configure_target(agentic_synth_jsi)
set_property(TARGET agentic_synth_jsi PROPERTY POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(agentic_synth_jsi PRIVATE -Werror)
endif()

if(AGENTIC_SYNTH_HAS_JSI)
    target_compile_definitions(agentic_synth_jsi PUBLIC AGENTIC_SYNTH_HAS_JSI=1)
endif()

find_package(Threads REQUIRED)
target_link_libraries(agentic_synth_jsi PRIVATE Threads::Threads)
