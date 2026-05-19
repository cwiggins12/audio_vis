#pragma once
// GL ES vs Desktop GL compile-time switch
//
// Build-system contract (set by CMake):
//   -DUSE_GLES=1   uses OpenGL ES 3.1   (Raspberry Pi, embedded)
//   (nothing)       uses Desktop OpenGL 4.4 core  (Linux x86, Windows)

//pick the right GL headers
#ifdef USE_GLES
    #include <GLES3/gl31.h>         // system-provided GL ES 3.1: no loader needed
    #include <GLES2/gl2ext.h>       // extension definitions
    #include <EGL/egl.h>            // for eglGetProcAddress

    // GL_EXT_buffer_storage — not in gl31.h, must be loaded manually
    #ifndef GL_MAP_PERSISTENT_BIT_EXT
        #define GL_MAP_PERSISTENT_BIT_EXT 0x0040
    #endif
    #ifndef GL_MAP_COHERENT_BIT_EXT
        #define GL_MAP_COHERENT_BIT_EXT   0x0080
    #endif

    typedef void (*PFNGLBUFFERSTORAGEEXTPROC)(GLenum, GLsizeiptr, const void*, GLbitfield);
    inline PFNGLBUFFERSTORAGEEXTPROC glBufferStorageEXT = nullptr;
#else
    #include <glad/glad.h>          // GLAD generated for GL 4.4 core
#endif

#include <GLFW/glfw3.h>
#include <iostream>

//GLSL version string to prepend
inline const char* glslVersionString() {
#ifdef USE_GLES
    return "#version 310 es\n";
#else
    return "#version 440 core\n";
#endif
}

//initialise GL loader
inline bool initGLAD() {
#ifdef USE_GLES
    // Load EXT_buffer_storage function pointer
    glBufferStorageEXT = (PFNGLBUFFERSTORAGEEXTPROC)
        eglGetProcAddress("glBufferStorageEXT");
    if (!glBufferStorageEXT) {
        glBufferStorageEXT = (PFNGLBUFFERSTORAGEEXTPROC)
            glfwGetProcAddress("glBufferStorageEXT");
    }
    if (!glBufferStorageEXT) {
        std::cerr << "Failed to load glBufferStorageEXT\n";
        return false;
    }
    return true;
#else
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialise GLAD (Desktop GL)\n";
        return false;
    }
    return true;
#endif
}

// ---- apply the correct GLFW window hints ----
inline void setGLFWContextHints(const GLFWvidmode* mode) {
#ifdef USE_GLES
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DOUBLEBUFFER,          GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_RED_BITS,              mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS,            mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS,             mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE,          mode->refreshRate);
    glfwWindowHint(GLFW_MAXIMIZED,             GLFW_TRUE);
}
