cmake_minimum_required(VERSION 3.16)

project(audio_vis LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
# Default to GLES on ARM (Raspberry Pi, embedded), desktop GL elsewhere.
# Override at configure time with -DUSE_GLES=ON or -DUSE_GLES=OFF.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    set(_default_gles ON)
else()
    set(_default_gles OFF)
endif()

option(USE_GLES "Use OpenGL ES 3.1 instead of desktop OpenGL" ${_default_gles})

message(STATUS "System:    ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
if(USE_GLES)
    message(STATUS "Graphics:  OpenGL ES 3.1 (no GLAD)")
else()
    message(STATUS "Graphics:  Desktop OpenGL (via GLAD loader)")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# GLFW: prefer the system package; fall back to building from source.
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "System GLFW not found -- fetching v3.4 from source")
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    FetchContent_MakeAvailable(glfw)
endif()

# Desktop GL only needed when not using GLES.
if(NOT USE_GLES)
    find_package(OpenGL REQUIRED)
endif()

# EGL is required either way (GLFW uses it on the Pi; harmless on desktop).
find_library(EGL_LIB EGL REQUIRED)

# GLES library (named GLESv2 even though it exposes 3.x entry points).
if(USE_GLES)
    find_library(GLES_LIB GLESv2 REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
set(AUDIO_VIS_SOURCES src/main.cpp)

# GLAD is only compiled in for the desktop GL path.
if(NOT USE_GLES)
    list(APPEND AUDIO_VIS_SOURCES src/external/glad/src/glad.c)
endif()

add_executable(audio_vis ${AUDIO_VIS_SOURCES})

target_include_directories(audio_vis PRIVATE
    src/include
    src/external
)

if(NOT USE_GLES)
    target_include_directories(audio_vis PRIVATE src/external/glad/include)
endif()

target_link_libraries(audio_vis PRIVATE
    glfw
    fftw3f
    ${EGL_LIB}
    dl
)

if(USE_GLES)
    target_link_libraries(audio_vis PRIVATE ${GLES_LIB})
    target_compile_definitions(audio_vis PRIVATE USE_GLES)
else()
    target_link_libraries(audio_vis PRIVATE OpenGL::GL)
endif()

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
