#define MINIAUDIO_IMPLEMENTATION

#include "audio.h"
#include "shader.h"
#include <SDL3/SDL.h>

//in dire need of some helpers once things start getting settled
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

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

    if (!gladLoadGLES2(SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    //1551
    //this should be variables set at window creation
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    glViewport(0, 0, w, h);

    //ties gl frames to device fps
    SDL_GL_SetSwapInterval(1);

    std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    const int hop_amt  = 2;
    const int fft_order = 11;
    //10px area around outside(x2), 10 to split meter area, 5 to split meters, 
    //20 per meter, and 5 to split those meters
    uint32_t fftOutSize = 1200;
    Audio audio(hop_amt, fft_order, fftOutSize);

    //my assumtion of passing fps to audio on init 
    //being cheap, easy, and consistent is not looking good here
    int displayHz = 60; //fallback :(
    SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayID);
    if (mode) displayHz = (int)mode->refresh_rate;
    if (!audio.init(displayHz)) {
        std::cerr << "Audio initialization failed \n";
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    const uint32_t numChannels = audio.getNumChannels();
    const uint32_t fftGpuOut = (fftOutSize == 0) ? audio.getAudibleSize() : fftOutSize;

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    Shader shader(vertexSrc, fragmentSrc);
    GLint timeLoc = glGetUniformLocation(shader.id, "time");

    GLuint ssbos[2];
    glGenBuffers(2, ssbos);

    GLint numBinsLoc = glGetUniformLocation(shader.id, "numBins");

    //SSBO 0: peak/rms - numChannels * 2 floats, tightly packed with std430
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, numChannels * 2 * sizeof(float), 
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbos[0]);

    //SSBO 1: FFT bins - audibleSize floats, tightly packed with std430
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, fftGpuOut * sizeof(float), 
                 nullptr, GL_DYNAMIC_DRAW);
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
                    w = event.window.data1;
                    h = event.window.data2;
                    glViewport(0, 0, w, h);
                    //look into a better way to resize this pls
                    //maybe when a customization struct comes in for shader switching
                    //have a variable scalar here
                    audio.resize(w);
                    break;
            }
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (audio.canAnalyze()) {
            audio.analyze();
        }
        audio.nextFrame();

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[0]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 
                        numChannels * 2 * sizeof(float), audio.getRMSPeakPtr());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos[1]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 
                        fftGpuOut * sizeof(float), audio.getSmoothFFTPtr());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


        shader.use();
        float t = SDL_GetTicks() / 1000.0f;
        glUniform1f(timeLoc, t);
        glUniform1i(numBinsLoc, fftGpuOut);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        //this blocks until next vblank and makes the loop fire once per device frame
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

