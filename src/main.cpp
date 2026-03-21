#define MINIAUDIO_IMPLEMENTATION

#include "audio.hpp"
#include "av_bridge.hpp"
#include "shader_loader.hpp"
#include <SDL3/SDL.h>

std::string getAssetPath(const std::string& relative) {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return relative; // fallback
    buf[len] = '\0';
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
}

void bindSSBO(int i, size_t size, GLuint b) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, 
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, b);
}

void dynBind(size_t size, GLuint b, const float* ptr) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, ptr);
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    //this will change eventually
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

    if (!gladLoadGLES2(SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    glViewport(0, 0, w, h);

    //ties gl frames to device fps
    SDL_GL_SetSwapInterval(1);

    //keep this around for Raspberry Pi testing l8r
    //std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
    //std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    //may wanna add some dynamic hopAmt or fftOrder changes to account for other rates
    int displayHz = 60; //fallback :(
    SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayID);
    if (mode) displayHz = (int)mode->refresh_rate;

    std::vector<ShaderPreset> presets = loadPresets(getAssetPath("shaders/"));
    if (presets.empty()) {
        std::cerr << "No valid presets found\n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    int activeIdx = 0;

    const int fft_order = 13;
    const int hopAmt = 4;

    Audio audio(fft_order, hopAmt);
    AVBridge bridge(audio, presets[0].spec);
    if (!audio.init(presets[0].spec)) {
        std::cerr << "Audio initialization failed \n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    bridge.init(displayHz, w, h);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint ssbos[4];
    glGenBuffers(4, ssbos);
    //look into how it handles the sizing changes
    //and better way to handle this type of op in general
    bindSSBO(0, bridge.getPeakRMSGPUSizeInBytes(), ssbos[0]);
    bindSSBO(1, bridge.getFFTGPUSizeInBytes(), ssbos[1]);
    bindSSBO(2, bridge.getPeakRMSGPUSizeInBytes(), ssbos[2]);
    bindSSBO(3, bridge.getFFTGPUSizeInBytes(), ssbos[3]);
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
                    if (event.key.key == SDLK_RIGHT) {
                        activeIdx = (activeIdx + 1) % presets.size();
                        bridge.swapSpec(presets[activeIdx].spec);
                        bindSSBO(0, bridge.getPeakRMSGPUSizeInBytes(), ssbos[0]);
                        bindSSBO(1, bridge.getFFTGPUSizeInBytes(),     ssbos[1]);
                        bindSSBO(2, bridge.getPeakRMSGPUSizeInBytes(), ssbos[2]);
                        bindSSBO(3, bridge.getFFTGPUSizeInBytes(),     ssbos[3]);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    w = event.window.data1;
                    h = event.window.data2;
                    glViewport(0, 0, w, h);
                    bridge.resize(w, h);
                    break;
            }
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (audio.canAnalyze()) {
            audio.analyze();
            bridge.formatData();
        }
        bridge.nextFrame();
        //h8 this so much
        size_t prSize = bridge.getPeakRMSGPUSizeInBytes();
        size_t fftSize = bridge.getFFTGPUSizeInBytes();
        dynBind(prSize, ssbos[0], bridge.getPeakRMSPtr());
        dynBind(fftSize, ssbos[1], bridge.getFFTPtr());
        if (presets[activeIdx].spec.getsPeakRMSHolds) {
            dynBind(prSize, ssbos[2], bridge.getPeakRMSHoldPtr());
        }
        if (presets[activeIdx].spec.getsFFTHolds) {
            dynBind(fftSize, ssbos[3], bridge.getFFTHoldPtr());
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        presets[activeIdx].shader.use();
        float t = SDL_GetTicks() / 1000.0f;
        glUniform1f(presets[activeIdx].shader.uniforms["time"], t);
        glUniform1i(presets[activeIdx].shader.uniforms["numBins"], 
                    bridge.getFFTGPUSize());
        glUniform1i(presets[activeIdx].shader.uniforms["numChannels"], 
                    audio.getNumChannels());
        glUniform1f(presets[activeIdx].shader.uniforms["H"], h);
        glUniform1f(presets[activeIdx].shader.uniforms["W"], w);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        //this blocks until next vblank and makes the loop fire once per device frame
        SDL_GL_SwapWindow(window);
    }
    glDeleteBuffers(4, ssbos);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

