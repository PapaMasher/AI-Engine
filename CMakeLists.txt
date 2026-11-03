cmake_minimum_required(VERSION 3.24)

project(Slate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(SLATE_BUILD_EDITOR "Build the Slate editor and runtime" ON)
option(SLATE_BUILD_TESTS "Build Slate unit and integration tests" ON)
option(SLATE_WARNINGS_AS_ERRORS "Treat Slate warnings as errors" OFF)
option(SLATE_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(Slate)

if(SLATE_BUILD_EDITOR)
    add_subdirectory(Slate-Editor)
    add_subdirectory(Slate-Runtime)
endif()

if(SLATE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

message(STATUS "Slate ${PROJECT_VERSION} configured: ${CMAKE_BUILD_TYPE}")
