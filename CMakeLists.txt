cmake_minimum_required(VERSION 3.16)
project(audio_vis LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# ---------------------------------------------------------------------------
# Architecture-specific optimization flags
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
	message(STATUS "Building for ARM64 (aarch64)")
	add_compile_options(-O3 -mcpu=native)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
	message(STATUS "Building for x86_64")
	add_compile_options(-O3 -march=native)
endif()
# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(glfw3 3.3 REQUIRED)
find_library(EGL_LIB EGL REQUIRED)
find_library(GLES_LIB GLESv2 REQUIRED)
# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(audio_vis
    src/main.cpp
    src/external/glad/src/glad.c
)
target_include_directories(audio_vis PRIVATE
    src/include
    src/external
    src/external/glad/include
)
target_link_libraries(audio_vis PRIVATE
    glfw
    fftw3f
    ${GLES_LIB}
    ${EGL_LIB}
    dl
)
# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SHADER_FILES
    ${CMAKE_SOURCE_DIR}/src/shaders/*.glsl
    ${CMAKE_SOURCE_DIR}/src/shaders/*.cfg
)
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/shaders
        ${CMAKE_BINARY_DIR}/shaders
    DEPENDS ${SHADER_FILES}
)
add_dependencies(audio_vis copy_shaders)
# ---------------------------------------------------------------------------
# Optional sanitizer
# ---------------------------------------------------------------------------
option(ASAN "Enable AddressSanitizer" OFF)
if(ASAN)
    target_compile_options(audio_vis PRIVATE -fsanitize=address -g)
    target_link_options(audio_vis    PRIVATE -fsanitize=address)
endif()
