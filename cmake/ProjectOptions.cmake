function(agentic_synth_configure_target target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)

  set_target_properties(
    ${target_name}
    PROPERTIES CXX_EXTENSIONS OFF
               CXX_STANDARD_REQUIRED ON)

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    target_compile_definitions(${target_name} PRIVATE NOMINMAX)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
