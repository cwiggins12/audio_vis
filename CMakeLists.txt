cmake_minimum_required(VERSION 3.16)

project(audio_vis LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(audio_vis
    src/main.cpp
    src/external/glad/src/glad.c
)

target_include_directories(audio_vis PRIVATE
    src/include			#custom headers
    src/external		#stb style headers
    src/external/glad/include	#glad loader
)

target_link_libraries(audio_vis PRIVATE
    fftw3f          		#float FFTW
    #dl              		#dynamic loading, not sure if needed, look into it more
    glfw	    		#glfw for windowing & event handling
    OpenGL::GL      		#OpenGL for context
    EGL             		#OpenGL dependency
)

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
