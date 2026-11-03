include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

file(GLOB_RECURSE SLATE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(Slate STATIC ${SLATE_SOURCES})
add_library(Slate::Slate ALIAS Slate)

target_include_directories(Slate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(Slate PUBLIC VK_NO_PROTOTYPES)
target_compile_definitions(Slate PUBLIC SLATE_ASSET_DIR="${CMAKE_BINARY_DIR}/Assets")
target_compile_definitions(Slate PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_compile_definitions(Slate PRIVATE $<$<CONFIG:Debug>:SLATE_DEBUG_BUILD>)

target_link_libraries(Slate
    PUBLIC
        glfw
        nvrhi_vk
        nvrhi
        glm
        box3d
        lua
        sol2
        cgltf
        stb
        nlohmann_json
        miniaudio
        volk
        imgui)

if(MSVC)
    target_compile_options(Slate PRIVATE /W4 /permissive- /utf-8)
    if(SLATE_WARNINGS_AS_ERRORS)
        target_compile_options(Slate PRIVATE /WX)
    endif()
else()
    target_compile_options(Slate PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    if(SLATE_WARNINGS_AS_ERRORS)
        target_compile_options(Slate PRIVATE -Werror)
    endif()
    if(WIN32)
        # Static runtime keeps distribution builds self-contained.
        target_link_options(Slate INTERFACE -static-libgcc -static-libstdc++)
    endif()
endif()

set_target_properties(Slate PROPERTIES FOLDER "Slate")

if(WIN32)
    target_link_libraries(Slate PUBLIC dbghelp ws2_32)
endif()

# Copy content into the build tree and compile shaders there. The engine
# resolves its asset root to this directory in development builds.
add_custom_target(SlateAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Assets ${CMAKE_BINARY_DIR}/Assets
    COMMENT "Copying Slate assets into the build tree")

add_dependencies(Slate SlateAssets)

slate_add_shaders(Slate
    ${CMAKE_BINARY_DIR}/Assets/Shaders
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/fullscreen.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/tonemap.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/pbr.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/pbr.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/shadow.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/prepass.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/prepass.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/ssao.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/ssao_blur.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/skybox.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/equirect_to_cube.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/irradiance.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/prefilter.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/brdf_lut.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/bloom_prefilter.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/bloom_downsample.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/bloom_upsample.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/bloom_combine.frag
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/ui.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/ui.frag)

add_dependencies(Slate_shaders SlateAssets)
