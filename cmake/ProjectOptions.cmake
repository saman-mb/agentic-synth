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
    # -Wno-missing-field-initializers because descriptor_dataset.h uses
    # designated init `{.field=val}` which only sets the fields each
    # descriptor wants — leaving others default-constructed is the
    # intent. GCC's -Wextra warns on every uninitialised field; the
    # noise drowns useful warnings.
    target_compile_options(${target_name} PRIVATE
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
  endif()
endfunction()
