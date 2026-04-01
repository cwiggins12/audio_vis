#define MINIAUDIO_IMPLEMENTATION

#include "audio.hpp"
#include "av_bridge.hpp"
#include "shader_loader.hpp"
#include "ssbo.hpp"
#include <GLFW/glfw3.h>

struct Log {
    std::streambuf* origCout;
    std::streambuf* origCerr;
    std::ofstream   logFile;

    Log(const std::string& path) {
        origCout = std::cout.rdbuf();
        origCerr = std::cerr.rdbuf();
        logFile.open(path);
        if (logFile.is_open()) {
            std::cout.rdbuf(logFile.rdbuf());
            std::cerr.rdbuf(logFile.rdbuf());
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
        }
        auto now = std::chrono::system_clock::now();
        std::time_t ts = std::chrono::system_clock::to_time_t(now);
        std::cout << "=== audio_vis started: " << std::ctime(&ts);
    }

    ~Log() {
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
    }
};

struct AudioSystem {
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << 13;
    static constexpr int fftBinAmt = fftSize / 2 + 1;
    static constexpr int hopAmt = 4;

    Audio    audio;
    AVBridge bridge;
    bool     valid = false;
    int      sampleRate = 0;
    int      channels = 0;

    AudioSystem(Spec& initSpec, int displayHz, int w, int h) :
                audio(fftOrder, hopAmt), bridge(audio, initSpec) {
        if (!audio.init(initSpec)) {
            std::cerr << "Audio initialization failed\n";
            return;
        }
        bridge.init(displayHz, w, h);
        sampleRate = audio.getSampleRate();
        channels = audio.getNumChannels();
        valid = true;
    }

    bool analyzeAndFormat() {
        bool newAudioWindow = false;
        if (audio.canAnalyze()) {
            audio.analyze();
            bridge.formatData();
            newAudioWindow = true;
        }
        bridge.nextFrame();
        return newAudioWindow;
    }

    void swap(Spec& spec) {
        audio.swapSpec(spec);
        bridge.swapSpec(spec);
    }
};

struct GLFWContext {
    GLFWwindow*        window    = nullptr;
    GLFWmonitor*       monitor   = nullptr;
    const GLFWvidmode* mode      = nullptr;
    int                displayHz = 60;
    bool               valid     = false;
    bool isFullscreen   = false;
    int windowedX = 0, windowedY = 0;
    int windowedW = 0, windowedH = 0;

    GLFWContext() {
        monitor = glfwGetPrimaryMonitor();
        mode = glfwGetVideoMode(monitor);
        if (!mode) {
            std::cerr << "Unable to get glfw vidmode\n";
            return;
        }
        displayHz = mode->refreshRate;
        glfwWindowHint(GLFW_CLIENT_API,             GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,  3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,  1);
        glfwWindowHint(GLFW_DOUBLEBUFFER,            GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE,               GLFW_TRUE);
        glfwWindowHint(GLFW_RED_BITS,                mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS,              mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS,               mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE,            displayHz);
        glfwWindowHint(GLFW_MAXIMIZED,               GLFW_TRUE);
        window = glfwCreateWindow(mode->width, mode->height, "audio_vis", nullptr, nullptr);
        if (!window) {
            std::cerr << "glfwCreateWindow failed\n";
            return;
        }
        glfwMakeContextCurrent(window);
        glfwGetWindowPos(window, &windowedX, &windowedY);
        valid = true;
    }

    ~GLFWContext() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }

    void initFramebuffer(int& w, int& h) {
        glfwGetFramebufferSize(window, &w, &h);
        windowedW = w; windowedH = h;
        glViewport(0, 0, w, h);
        glfwSwapInterval(1);
    }

    void logGLInfo() {
        //print info to log
        std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
        std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
        std::cout << "Found device frame rate: " << displayHz << std::endl;
    }

    void toggleFullscreen() {
        if (!isFullscreen) {
            glfwGetWindowPos(window, &windowedX, &windowedY);
            glfwGetWindowSize(window, &windowedW, &windowedH);
            //hacky way to get monitor with most overlap. Needs more testing
            monitor = getCurrentMonitor();
            mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0,
                                 mode->width, mode->height, mode->refreshRate);
        }
        else {
            glfwSetWindowMonitor(window, nullptr, windowedX, windowedY,
                                 windowedW, windowedH, 0);
        }
        isFullscreen = !isFullscreen;
    }

    void checkForResize(AudioSystem& a, ShaderPreset* p, int& w, int& h, bool& needsSwap) {
        int newW, newH;
        glfwGetFramebufferSize(window, &newW, &newH);
        if (newW != w || newH != h) {
            w = newW;
            h = newH;
            glViewport(0, 0, w, h);
            a.bridge.resize(w, h);
            if (p->spec.fftUsesExprVar[WINDOW_WIDTH] ||
                p->spec.fftUsesExprVar[WINDOW_HEIGHT] ||
                p->spec.customFFTSizeScalesWithWindow ||
                p->spec.feedbackUsesExprVar[WINDOW_WIDTH] ||
                p->spec.feedbackUsesExprVar[WINDOW_HEIGHT] ||
                p->spec.feedbackBufferScalesWithWindow) {
                needsSwap = true;
            }
        }
    }

    void checkForFrameRateChange(ShaderPreset* p, bool& needsSwap) {
        monitor = glfwGetWindowMonitor(window);
        if (monitor) {
            mode = glfwGetVideoMode(monitor);
            if (mode && displayHz != mode->refreshRate &&
                (p->spec.fftUsesExprVar[DISPLAY_HZ] ||
                p->spec.feedbackUsesExprVar[DISPLAY_HZ])) {
                displayHz = mode->refreshRate;
                needsSwap = true;
            }
        }
    }

    void setTitleBarForPreset(int i, std::string& s) {
        std::string newTitle = "audio_vis - Preset " + std::to_string(i) + ": " + s;
        glfwSetWindowTitle(window, newTitle.c_str());
    }

    GLFWmonitor* getCurrentMonitor() {
        int wx, wy, ww, wh;
        glfwGetWindowPos(window, &wx, &wy);
        glfwGetWindowSize(window, &ww, &wh);

        int monitorCount;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

        GLFWmonitor* best = glfwGetPrimaryMonitor();
        int bestOverlap = 0;

        for (int i = 0; i < monitorCount; i++) {
            int mx, my;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* vm = glfwGetVideoMode(monitors[i]);

            int olX = std::max(0, std::min(wx + ww, mx + vm->width) - std::max(wx, mx));
            int olY = std::max(0, std::min(wy + wh, my + vm->height) - std::max(wy, my));
            int overlap  = olX * olY;

            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                best = monitors[i];
            }
        }
        return best;
    }
};

struct GPUBuffers {
    GLuint vao;
    SSBO   ssbos[6];
    bool   feedbackFlip = false;

    GPUBuffers(float fbInitVal) {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        ssbos[0].alloc(16); ssbos[0].bind(0);
        ssbos[1].alloc(16); ssbos[1].bind(1);
        ssbos[2].alloc(16); ssbos[2].bind(2);
        ssbos[3].alloc(16); ssbos[3].bind(3);
        ssbos[4].alloc(16); ssbos[4].fill(fbInitVal); ssbos[4].bind(4);
        ssbos[5].alloc(16); ssbos[5].fill(fbInitVal); ssbos[5].bind(5);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void writeToBuffers(AVBridge& bridge, Spec& spec) {
        //write to gpu buffers
        size_t prSize  = bridge.getPeakRMSGPUSizeInBytes();
        size_t fftSize = bridge.getFFTGPUSizeInBytes();
        ssbos[0].write(bridge.getPeakRMSPtr(), prSize);
        ssbos[1].write(bridge.getFFTPtr(), fftSize);
        if (spec.getsPeakRMSHolds) {
            ssbos[2].write(bridge.getPeakRMSHoldPtr(), prSize);
        }
        if (spec.getsFFTHolds) {
            ssbos[3].write(bridge.getFFTHoldPtr(), fftSize);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    }

    void flipFeedback() {
        feedbackFlip = !feedbackFlip;
        ssbos[4].bind(feedbackFlip ? 4 : 5);
        ssbos[5].bind(feedbackFlip ? 5 : 4);
    }

    ~GPUBuffers() {
        glDeleteVertexArrays(1, &vao);
    }
};

struct ShaderSystem {
    std::vector<ShaderPreset> presets;
    ShaderPreset*             active = nullptr;
    Shader                    error;
    int                       index = 0;
    bool                      valid = false;

    ShaderSystem(const std::string& shaderPath) {
        presets = loadPresets(shaderPath);
        if (presets.empty()) {
            std::cerr << "No valid presets found\n";
            return;
        }
        if (!error.init(vertexSrc, errorFragSrc).empty()) {
            std::cout << "Failed compilation of error shader. "
                         "Hot reloads will be UB until errorFragSrc is fixed\n";
        }
        active = &presets[0];
        valid = true;
    }

    void setIndex(int i) {
        index  = i;
        active = &presets[index];
    }

    void useErrorShader() {
        error.use();
        int chars[128] = {};
        std::string msg = active->errorMessage;
        int len = std::min((int)msg.size(), 128);
        for (int i = 0; i < len; i++) {
            chars[i] = (int)msg[i];
        }
        glUniform1i(error.uniforms[U_ERROR_LEN], len);
        glUniform1i(error.uniforms[U_SHOW_ERROR], 1);
        glUniform1iv(error.uniforms[U_ERROR_CHARS], 128, chars);
    }

    void useActiveShader(float t, AudioSystem& a, int h, int w, bool newAudioWindow, int hz) {
        active->shader.use();
        glUniform1f(active->shader.uniforms[U_TIME], t);
        glUniform1i(active->shader.uniforms[U_FFT_SIZE], a.fftSize);
        glUniform1i(active->shader.uniforms[U_FFT_BIN_AMT], a.fftBinAmt);
        glUniform1i(active->shader.uniforms[U_FFT_ARR_SIZE], a.bridge.getFFTGPUSize());
        glUniform1i(active->shader.uniforms[U_NEW_AUDIO_WINDOW], newAudioWindow);
        glUniform1i(active->shader.uniforms[U_NUM_CHANNELS], a.channels);
        glUniform1f(active->shader.uniforms[U_H], (float)h);
        glUniform1f(active->shader.uniforms[U_W], (float)w);
        glUniform1i(active->shader.uniforms[U_SAMPLE_RATE], a.sampleRate);
        glUniform1i(active->shader.uniforms[U_DISPLAY_HZ], hz);
        glUniform1i(active->shader.uniforms[U_SHOW_ERROR], 0);
        bindTextures(active);
    }
};

struct InputHandler {
    int prevRightKey    = GLFW_RELEASE;
    int prevLeftKey     = GLFW_RELEASE;
    int prevFSKey       = GLFW_RELEASE;
    int presetsSize     = 0;

    InputHandler(int size) {
        presetsSize = size;
    }

    void handleInput(GLFWContext& glfw, ShaderSystem& s, bool& needsSwap) {
        glfwPollEvents();
        //check for escape press
        if (glfwGetKey(glfw.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(glfw.window, GLFW_TRUE);
        }
        //check right key
        int rightKey = glfwGetKey(glfw.window, GLFW_KEY_RIGHT);
        if (rightKey == GLFW_PRESS && prevRightKey == GLFW_RELEASE) {
            s.setIndex((s.index + 1) % presetsSize);
            needsSwap = true;
        }
        prevRightKey = rightKey;
        //check left key
        int leftKey = glfwGetKey(glfw.window, GLFW_KEY_LEFT);
        if (leftKey == GLFW_PRESS && prevLeftKey == GLFW_RELEASE) {
            s.setIndex(((s.index - 1) + (int)presetsSize) % (int)presetsSize);
            needsSwap = true;
        }
        prevLeftKey = leftKey;
        //check up key for fullscreen
        int fsKey = glfwGetKey(glfw.window, GLFW_KEY_UP);
        if (fsKey == GLFW_PRESS && prevFSKey == GLFW_RELEASE) {
            glfw.toggleFullscreen();
        }
        prevFSKey = fsKey;
    }
};

std::string getAssetPath(const std::string& relative) {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return relative;
    buf[len] = '\0';
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
}

std::string evalSpecExprs(Spec& spec, ExprContext& ctx) {
    std::string ret = evalExpr(spec.customFFTSizeExpr, ctx,
                               spec.customFFTSize, spec.fftUsesExprVar);
    if (!ret.empty()) return ret;
    ret = evalExpr(spec.feedbackBufferSizeExpr, ctx,
                   spec.feedbackBufferSize, spec.feedbackUsesExprVar);
    return ret;
}

bool evalPresetExprs(int w, int h, int hz, AudioSystem& a, ShaderPreset* pre) {
    ExprContext ctx;
    ctx.windowWidth  = w;
    ctx.windowHeight = h;
    ctx.numChannels  = a.channels;
    ctx.displayHz    = hz;
    ctx.sampleRate   = a.sampleRate;
    ctx.fftSize      = a.fftSize;
    std::string ret = evalSpecExprs(pre->spec, ctx);
    if (!ret.empty()) {
        pre->errorMessage = ret;
        pre->hasError = true;
        return false;
    }
    return true;
}

void hotReloadCheck(ShaderPreset* p, bool& needsSwap) {
    auto fragPath = std::filesystem::path(p->shaderDir) / "frag.glsl";
    auto specPath = std::filesystem::path(p->shaderDir) / "spec.cfg";
    if (!fragPath.empty()
        && std::filesystem::exists(fragPath)) {
        auto fragTime = std::filesystem::last_write_time(fragPath);
        std::filesystem::file_time_type specTime{};
        if (!specPath.empty() 
            && std::filesystem::exists(specPath)) {
            specTime = std::filesystem::last_write_time(specPath);
        }
        if (fragTime != p->lastFragWrite
            || specTime != p->lastSpecWrite) {
            p->lastFragWrite = fragTime;
            p->lastSpecWrite = specTime;
            std::cout << "hot reload: " << p->name << "\n";
            reloadPreset(p);
            if (!p->hasError) {
                needsSwap = true;
            }
        }
    }
}


void doSwap(ShaderPreset* p, AudioSystem& a, SSBO* ssbos) {
    a.swap(p->spec);
    ssbos[0].resize(a.bridge.getPeakRMSGPUSizeInBytes()); ssbos[0].bind(0);
    ssbos[1].resize(a.bridge.getFFTGPUSizeInBytes());     ssbos[1].bind(1);
    ssbos[2].resize(a.bridge.getPeakRMSGPUSizeInBytes()); ssbos[2].bind(2);
    ssbos[3].resize(a.bridge.getFFTGPUSizeInBytes());     ssbos[3].bind(3);
    size_t fbSize = p->spec.feedbackBufferSize * sizeof(float);
    int mode = p->spec.feedbackBufferScalesWithWindow;
    fbSize = a.bridge.getSizeFromModeSwitch(fbSize, mode);
    float fbInit = p->spec.feedbackBufferInitValue;
    ssbos[4].resize(fbSize);    ssbos[4].fill(fbInit);  ssbos[4].bind(4);
    ssbos[5].resize(fbSize);    ssbos[5].fill(fbInit);  ssbos[5].bind(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

int main() {
    //init log file writer
    Log log(getAssetPath("log.txt"));
    //init glfw, monitor, window, mode, and displayHz
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return -1; }
    GLFWContext glfw;
    if (!glfw.valid) return -1;
    //init glad
    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }
    //get initial width and height from framebuffer then log gl info
    int w, h;
    glfw.initFramebuffer(w, h);
    glfw.logGLInfo();
    //init shaders
    ShaderSystem shaders(getAssetPath("shaders/"));
    if (!shaders.valid) return -1;
    //init audio
    AudioSystem audioSys(shaders.active->spec, glfw.displayHz, w, h);
    if (!audioSys.valid) return -1;
    glfw.setTitleBarForPreset(shaders.index, shaders.active->name);
    //init gpu verts and buffers
    GPUBuffers gpuBuffs(shaders.active->spec.feedbackBufferInitValue);
    //swap all configs to first preset, unless eval error, then use errorShader
    if (evalPresetExprs(w, h, glfw.displayHz, audioSys, shaders.active) &&
        assertUserDefinedBufferSizes(shaders.active)) {
        doSwap(shaders.active, audioSys, gpuBuffs.ssbos);
    }
    //catches button presses and handles them
    InputHandler input(shaders.presets.size());
    //per frame loop
    while (!glfwWindowShouldClose(glfw.window)) {
        //flag for swap
        bool needsSwap = false;
        //poll for input and handle it
        input.handleInput(glfw, shaders, needsSwap);
        //check for resize
        glfw.checkForResize(audioSys, shaders.active, w, h, needsSwap);
        //check for frame rate change
        glfw.checkForFrameRateChange(shaders.active, needsSwap);
        // hot reload if necessary
        hotReloadCheck(shaders.active, needsSwap);
        //do swap if necessary, if eval fails, update activeIndex error msg
        if (needsSwap) {
            if (evalPresetExprs(w, h, glfw.displayHz, audioSys, shaders.active) &&
                assertUserDefinedBufferSizes(shaders.active)) {
                std::cout << "Swapping to: " << shaders.active->name << "\n";
                doSwap(shaders.active, audioSys, gpuBuffs.ssbos);
            }
            glfw.setTitleBarForPreset(shaders.index, shaders.active->name);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        //clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        //either analyze new frame and format, or just temporally move values to send
        bool newAudioWindow = audioSys.analyzeAndFormat();
        //write to gpu buffers
        gpuBuffs.writeToBuffers(audioSys.bridge, shaders.active->spec);
        //use shader based on error state
        if (shaders.active->hasError) {
            shaders.useErrorShader();
        }
        else {
            shaders.useActiveShader((float)glfwGetTime(), audioSys,
                                    h, w, newAudioWindow, glfw.displayHz);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        //flip for user defined feedback ssbo
        gpuBuffs.flipFeedback();
        //not sure if necessary
        unbindTextures(shaders.active);
        //set swap and count frame counter
        glfwSwapBuffers(glfw.window);
    }
    return 0;
}

