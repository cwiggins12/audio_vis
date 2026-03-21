cmake_minimum_required(VERSION 3.16)

project(audio_vis LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(audio_vis
    src/main.cpp
    src/external/glad/src/gles2.c
    src/external/glad/src/egl.c
)

target_include_directories(audio_vis PRIVATE
    src/include
    src/external/glad/include
)

target_link_libraries(audio_vis PRIVATE
    fftw3f          #float FFTW
    pthread         #required by miniaudio
    dl              #required on Linux
    m               #math library
    SDL3::SDL3      #SDL3 for windowing
    OpenGL::GL      #OpenGL for context
    EGL             #OpenGL dependency
)

file(GLOB_RECURSE SHADER_FILES
    ${CMAKE_SOURCE_DIR}/src/shaders/*.glsl
    ${CMAKE_SOURCE_DIR}/src/shaders/*.txt
)

add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/shaders
        ${CMAKE_BINARY_DIR}/shaders
    DEPENDS ${SHADER_FILES}
)

add_dependencies(audio_vis copy_shaders)
