#define MINIAUDIO_IMPLEMENTATION

#include "audio.h"
#include <csignal>
#include <SDL3/SDL.h>
#include <iostream>
#include <glad/gles2.h>

class Shader {
public:
    GLuint id;

    Shader(const char* vertSrc, const char* fragSrc) {
        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);

        GLint success;
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(id, 512, nullptr, log);
            std::cerr << "Shader link error:\n" << log << std::endl;
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() {
        glUseProgram(id);
    }

private:
    GLuint compile(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "Shader compile error:\n" << log << std::endl;
        }

        return shader;
    }
};

const char* vertexSrc = R"(#version 310 es
precision highp float;

out vec2 uv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexID];
    uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

/*
const char* fragmentSrc = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
};

layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};

void main() {
    vec3 color = 0.5 + 0.5 * cos(time + uv.xyx + vec3(0,2,4));
    FragColor = vec4(color, 1.0);
}
)";
*/

const char* fragmentSrc = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;
uniform int numBins;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
};

layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};

void main() {
    //map uv.x to a bin index
    int binIndex = int(uv.x * float(numBins));
    binIndex = clamp(binIndex, 0, numBins - 1);

    float db = fftData[binIndex];

    //normalize -120..0 to 0..1
    float normalized =(db + 120.0) / 120.0;
    normalized = clamp(normalized, 0.0, 1.0);

    //draw bar - 1.0 if uv.y is below the bar height, 0.0 otherwise
    float bar = step(uv.y, normalized);

    //color: gradient from blue at bottom to cyan at top
    vec3 color = mix(vec3(0.0, 0.2, 0.8), vec3(0.0, 1.0, 0.9), uv.y) * bar;

    //dark background where there's no bar
    color += vec3(0.05, 0.07, 0.12) * (1.0 - bar);

    FragColor = vec4(color, 1.0);
}
)";

int main() {
    const int hop_amt  = 4;
    const int fft_order = 11;
    Audio audio(hop_amt, fft_order);
    if (!audio.init()) {
        std::cerr << "Audio initialization failed \n";
        return -1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    //probably need this to scale and start full screen eventually, we'll see
    SDL_Window* window = SDL_CreateWindow("audio_vis", 1280, 720,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "GL Context creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    //ties gl frames to device fps
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLES2(SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    //comment out eventually
    std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    const unsigned int numChannels = audio.getNumChannels();
    //set up a better way to do this in fft pls
    const unsigned int numAudibleBins = audio.getAudibleSize();

    //this should be variables set at window creation
    glViewport(0, 0, 1280, 720);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    Shader shader(vertexSrc, fragmentSrc);
    GLint timeLoc = glGetUniformLocation(shader.id, "time");

    GLuint ssbos[2];
    glGenBuffers(2, ssbos);

    GLint numBinsLoc = glGetUniformLocation(shader.id, "numBins");
    glUniform1i(numBinsLoc, numAudibleBins);

    //SSBO 0: peak/rms - numChannels * 2 floats, tightly packed with std430
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, numChannels * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbos[0]);

    //SSBO 1: FFT bins - audibleSize floats, tightly packed with std430
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, numAudibleBins * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbos[1]);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE)
                        running = false;
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    glViewport(0, 0, event.window.data1, event.window.data2);
                    break;
            }
        }

        glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (audio.canAnalyze()) {
            audio.analyze();

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[0]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numChannels * 2 * sizeof(float), audio.getRMSPeakPtr());

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[1]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numAudibleBins * sizeof(float), audio.getFFTPtr());

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        shader.use();
        float t = SDL_GetTicks() / 1000.0f;
        glUniform1f(timeLoc, t);
        glUniform1i(numBinsLoc, numAudibleBins);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        SDL_GL_SwapWindow(window);
    }

    glDeleteBuffers(2, ssbos);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Stopped. :)\n");
    return 0;
}
