# Compiles GLSL shaders to SPIR-V at build time using glslangValidator.
#
# Usage:
#   slate_add_shaders(<target> <output_dir> <shader1.glsl> [<shader2.glsl> ...])
#
# Each shader is compiled with the Vulkan 1.2 target environment. The resulting
# .spv files are placed in <output_dir> and copied next to the executable at
# build time. Shaders are recompiled automatically when the GLSL source changes.

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslangValidator.exe
    HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
    DOC "glslangValidator executable used to compile GLSL to SPIR-V")

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "glslangValidator was not found. Install the Vulkan SDK (or the MSYS2 "
        "mingw-w64-x86_64-glslang package) and ensure it is on PATH.")
endif()

function(slate_add_shaders target output_dir)
    set(spv_files)
    foreach(shader IN LISTS ARGN)
        get_filename_component(shader_name ${shader} NAME_WE)
        get_filename_component(shader_ext ${shader} EXT)
        set(spv_file ${output_dir}/${shader_name}${shader_ext}.spv)
        add_custom_command(
            OUTPUT ${spv_file}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2
                    -o ${spv_file} ${shader}
            DEPENDS ${shader}
            COMMENT "Compiling shader ${shader_name}${shader_ext} -> SPIR-V"
            VERBATIM)
        list(APPEND spv_files ${spv_file})
    endforeach()
    add_custom_target(${target}_shaders ALL DEPENDS ${spv_files})
    add_dependencies(${target} ${target}_shaders)
endfunction()
