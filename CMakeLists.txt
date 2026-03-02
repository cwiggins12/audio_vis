cmake_minimum_required(VERSION 3.16)

project(audio_vis LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(audio_vis
    main.cpp
    external/glad/src/gles2.c
    external/glad/src/egl.c
)

# Include current directory (for miniaudio.h and headers)
target_include_directories(audio_vis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

# Link libraries
target_link_libraries(audio_vis PRIVATE
    fftw3f          #float FFTW
    pthread         #required by miniaudio
    dl              #required on Linux
    m               #math library
    SDL3::SDL3      #SDL3 for windowing
    OpenGL::GL      #OpenGL for context
    EGL             #OpenGL dependency
)

