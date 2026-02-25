cmake_minimum_required(VERSION 3.16)

project(audio_vis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(audio_vis
    main.cpp
)

# Include current directory (for miniaudio.h and headers)
target_include_directories(audio_vis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(audio_vis PRIVATE
    fftw3f        #float FFTW
    pthread       #required by miniaudio
    dl            #required on Linux
    m             #math library
    SDL3::SDL3    #SDL3
)

