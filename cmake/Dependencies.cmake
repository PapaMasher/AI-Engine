# Third-party dependencies. All are vendored under third_party/ so the build is
# reproducible and works offline. Versions are recorded in third_party/README.md.

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# Vulkan headers (vulkan-hpp + vulkan.h). NVRHI's Vulkan backend requires the
# Vulkan::Headers target; we provide it rather than letting NVRHI fetch it.
add_subdirectory(third_party/Vulkan-Headers)

set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/nvrhi)

# glm: header-only math library.
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm)

# Box3D physics (the 3D successor to Box2D v3). Build the library only;
# samples, tests and benchmarks are not needed here.
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX3D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX3D_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/box3d ${CMAKE_BINARY_DIR}/third_party/box3d EXCLUDE_FROM_ALL)

# Lua 5.4.7 interpreter, built as a static library without the standalone
# interpreter and compiler front-ends.
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
list(REMOVE_ITEM LUA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/lua.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/luac.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)
if(WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_WINDOWS)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
else()
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
endif()
set_target_properties(lua PROPERTIES FOLDER "ThirdParty")

# Dear ImGui (docking branch) with the GLFW and Vulkan backends. The Vulkan
# backend is compiled against volk because the engine never links the Vulkan
# loader directly.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK VK_NO_PROTOTYPES IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC glfw Vulkan::Headers volk)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# volk: meta-loader for Vulkan. Required because we never link vulkan-1.lib;
# the loader is resolved at runtime, which keeps builds portable.
add_library(volk STATIC third_party/volk/volk.c)
target_include_directories(volk PUBLIC third_party/volk)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(volk PUBLIC Vulkan::Headers)
if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
elseif(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_MACOS_MVK)
else()
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
endif()
set_target_properties(volk PROPERTIES FOLDER "ThirdParty")

# Header-only dependencies exposed as interface targets.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE third_party/doctest)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE third_party/cgltf)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE third_party/stb)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party/json)

add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE third_party/sol2/include)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE third_party)
