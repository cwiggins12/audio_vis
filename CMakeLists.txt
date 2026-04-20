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
# GL ES vs Desktop GL selection
#
#   -DUSE_GLES=ON   forces GL ES 3.1  (always used on Pi / aarch64 Linux)
#   -DUSE_GLES=OFF  forces Desktop GL 4.4 core
#   (default)        auto-detect: aarch64 + Linux → ES, everything else → Desktop
# ---------------------------------------------------------------------------
if(NOT DEFINED USE_GLES)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(USE_GLES ON)
    else()
        set(USE_GLES OFF)
    endif()
endif()
message(STATUS "USE_GLES = ${USE_GLES}")
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
if(WIN32)
    # Windows: prebuilt libs in libs/ directory
    #
    # Required layout:
    #   libs/glfw/include/GLFW/glfw3.h
    #   libs/glfw/libglfw3.a             (MinGW static library)
    #   libs/fftw/fftw3.h
    #   libs/fftw/libfftw3f-3.dll
    set(GLFW_DIR "${CMAKE_SOURCE_DIR}/libs/glfw")
    set(FFTW_DIR "${CMAKE_SOURCE_DIR}/libs/fftw")

    # GLFW (static)
    add_library(glfw STATIC IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${GLFW_DIR}/libglfw3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
    )

    # FFTW (link directly against DLL — MinGW handles this natively)
    add_library(fftw3f SHARED IMPORTED)
    set_target_properties(fftw3f PROPERTIES
        IMPORTED_IMPLIB "${FFTW_DIR}/libfftw3f-3.dll"
        IMPORTED_LOCATION "${FFTW_DIR}/libfftw3f-3.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${FFTW_DIR}"
    )

    find_package(OpenGL REQUIRED)
else()
    # Linux / Pi: use system packages
    find_package(glfw3 3.3 REQUIRED)

    if(USE_GLES)
        find_library(EGL_LIB EGL REQUIRED)
        find_library(GLES_LIB GLESv2 REQUIRED)
    else()
        find_package(OpenGL REQUIRED)
    endif()
endif()
# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(audio_vis
    src/main.cpp
)
target_include_directories(audio_vis PRIVATE
    src/include
    src/external
)

if(USE_GLES)
    # GL ES — no GLAD, system provides everything
else()
    # Desktop GL — need GLAD loader
    target_sources(audio_vis PRIVATE src/external/glad/src/glad.c)
    target_include_directories(audio_vis PRIVATE src/external/glad/include)
endif()
# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
if(USE_GLES)
    target_compile_definitions(audio_vis PRIVATE USE_GLES=1)
endif()
# ---------------------------------------------------------------------------
# Linking
# ---------------------------------------------------------------------------
if(USE_GLES)
    target_link_libraries(audio_vis PRIVATE
        glfw
        fftw3f
        ${GLES_LIB}
        ${EGL_LIB}
        dl
    )
elseif(WIN32)
    target_link_libraries(audio_vis PRIVATE
        glfw
        fftw3f
        OpenGL::GL
    )
else()
    target_link_libraries(audio_vis PRIVATE
        glfw
        fftw3f
        OpenGL::GL
        dl
    )
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
# Windows: copy FFTW DLL next to exe
# ---------------------------------------------------------------------------
if(WIN32)
    add_custom_command(TARGET audio_vis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFTW_DIR}/libfftw3f-3.dll"
            $<TARGET_FILE_DIR:audio_vis>
    )
endif()
# ---------------------------------------------------------------------------
# Optional sanitizer
# ---------------------------------------------------------------------------
option(ASAN "Enable AddressSanitizer" OFF)
if(ASAN)
    target_compile_options(audio_vis PRIVATE -fsanitize=address -g)
    target_link_options(audio_vis    PRIVATE -fsanitize=address)
endif()