#define MINIAUDIO_IMPLEMENTATION

#include "audio.hpp"
#include "av_bridge.hpp"
#include "shader_loader.hpp"
#include "ssbo.hpp"
#include <GLFW/glfw3.h>

std::string getAssetPath(const std::string& relative) {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return relative;
    buf[len] = '\0';
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
}

void setTitleBarForPreset(GLFWwindow* window, int i, std::string& s) {
    std::string newTitle = "audio_vis - Preset " + std::to_string(i) + ": " + s;
    glfwSetWindowTitle(window, newTitle.c_str());
}

GLFWmonitor* getCurrentMonitor(GLFWwindow* window) {
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

void doSwap(int activeIdx, std::vector<ShaderPreset>& presets,
            AVBridge& bridge, SSBO* ssbos) {
    bridge.swapSpec(presets[activeIdx].spec);
    ssbos[0].resize(bridge.getPeakRMSGPUSizeInBytes()); ssbos[0].bind(0);
    ssbos[1].resize(bridge.getFFTGPUSizeInBytes());     ssbos[1].bind(1);
    ssbos[2].resize(bridge.getPeakRMSGPUSizeInBytes()); ssbos[2].bind(2);
    ssbos[3].resize(bridge.getFFTGPUSizeInBytes());     ssbos[3].bind(3);
    size_t fbSize = presets[activeIdx].spec.feedbackBufferSize * sizeof(float);
    int mode = presets[activeIdx].spec.feedbackBufferScalesWithWindow;
    fbSize = bridge.getSizeFromModeSwitch(fbSize, mode);
    float fbInit = presets[activeIdx].spec.feedbackBufferInitValue;
    ssbos[4].resize(fbSize);    ssbos[4].fill(fbInit);  ssbos[4].bind(4);
    ssbos[5].resize(fbSize);    ssbos[5].fill(fbInit);  ssbos[5].bind(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

ExprContext makeExprContext(int w, int h, int displayHz, Audio& audio) {
    ExprContext ctx;
    ctx.windowWidth  = w;
    ctx.windowHeight = h;
    ctx.numChannels  = audio.getNumChannels();
    ctx.displayHz    = displayHz;
    ctx.sampleRate   = audio.getSampleRate();
    ctx.fftSize      = audio.getFFTSize();
    return ctx;
}

std::string evalSpecExprs(Spec& spec, ExprContext& ctx) {
    std::string ret = evalExpr(spec.customFFTSizeExpr, ctx,
                               spec.customFFTSize, spec.fftUsesExprVar);
    if (!ret.empty()) return ret;
    ret = evalExpr(spec.feedbackBufferSizeExpr, ctx,
                   spec.feedbackBufferSize, spec.feedbackUsesExprVar);
    return ret;
}

bool evalPresetExprs(int w, int h, int hz, Audio& audio, ShaderPreset& pre) {
    ExprContext ctx = makeExprContext(w, h, hz, audio);
    std::string ret = evalSpecExprs(pre.spec, ctx);
    if (!ret.empty()) {
        pre.errorMessage = ret;
        pre.hasError = true;
        return false;
    }
    return true;
}

int main() {
    //init log file writer
    std::streambuf* origCout = std::cout.rdbuf();
    std::streambuf* origCerr = std::cerr.rdbuf();
    std::ofstream logFile(getAssetPath("log.txt"));
    if (logFile.is_open()) {
        std::cout.rdbuf(logFile.rdbuf());
        std::cerr.rdbuf(logFile.rdbuf());
        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);
    }
    auto now = std::chrono::system_clock::now();
    std::time_t ts = std::chrono::system_clock::to_time_t(now);
    std::cout << "=== audio_vis started: " << std::ctime(&ts);
    //init glfw, monitor, and window
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();

        return -1;
    }
    int displayHz = 60;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        std::cerr << "Unable to get glfw vidmode\n";
        glfwTerminate();
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
        return -1;
    }
    displayHz = mode->refreshRate;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, displayHz);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "audio_vis",
                                          nullptr, nullptr);
    if (!window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
        return -1;
    }
    glfwMakeContextCurrent(window);
    //init glad
    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
        return -1;
    }
    //get initial width and height
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glfwSwapInterval(1);
    //print info to log
    std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
    std::cout << "Found device frame rate: " << displayHz << std::endl;
    //init shaders
    std::vector<ShaderPreset> presets = loadPresets(getAssetPath("shaders/"));
    if (presets.empty()) {
        std::cerr << "No valid presets found\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
        return -1;
    }
    Shader errorShader(vertexSrc, errorFragSrc);
    int activeIdx = 0;
    //set up a way to define these based on displayHz?
    const int fft_order = 13;
    const int hopAmt = 4;
    //Init audio and audio formatting
    Audio audio(fft_order, hopAmt);
    AVBridge bridge(audio, presets[0].spec);
    if (!audio.init(presets[0].spec)) {
        std::cerr << "Audio initialization failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    bridge.init(displayHz, w, h);
    setTitleBarForPreset(window, activeIdx, presets[activeIdx].name);
    int sampleRate = audio.getSampleRate();
    //init gpu verts and buffers
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    SSBO ssbos[6];
    ssbos[0].alloc(16);         ssbos[0].bind(0);
    ssbos[1].alloc(16);         ssbos[1].bind(1);
    ssbos[2].alloc(16);         ssbos[2].bind(2);
    ssbos[3].alloc(16);         ssbos[3].bind(3);
    float fbInitVal = presets[0].spec.feedbackBufferInitValue;
    ssbos[4].alloc(16);     ssbos[4].fill(fbInitVal);       ssbos[4].bind(4);
    ssbos[5].alloc(16);     ssbos[5].fill(fbInitVal);       ssbos[5].bind(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    //swap all configs to first preset, unless eval error, then use errorShader
    if (evalPresetExprs(w, h, displayHz, audio, presets[0]) &&
        assertUserDefinedBufferSizes(presets[0])) {
        doSwap(0, presets, bridge, ssbos);
    }
    //full setup of vars to check for resize and fullscreen swaps
    int windowedX = 0, windowedY = 0, windowedW = w, windowedH = h;
    glfwGetWindowPos(window, &windowedX, &windowedY);
    //store prevs and multi frame vars
    int prevRightKey = GLFW_RELEASE;
    int prevLeftKey = GLFW_RELEASE;
    int prevFSKey = GLFW_RELEASE;
    bool isFullscreen = false;
    bool feedbackFlip = false;
    int frameCounter = 0;
    //the real program
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        //swap shader flag
        bool needsSwap = false;
        //check for escape press
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        //check right key
        int rightKey = glfwGetKey(window, GLFW_KEY_RIGHT);
        if (rightKey == GLFW_PRESS && prevRightKey == GLFW_RELEASE) {
            activeIdx = (activeIdx + 1) % presets.size();
            needsSwap = true;
        }
        prevRightKey = rightKey;
        //check left key
        int leftKey = glfwGetKey(window, GLFW_KEY_LEFT);
        if (leftKey == GLFW_PRESS && prevLeftKey == GLFW_RELEASE) {
            activeIdx = ((activeIdx - 1) + (int)presets.size()) % (int)presets.size();
            needsSwap = true;
        }
        prevLeftKey = leftKey;
        //check up key for fullscreen
        int fsKey = glfwGetKey(window, GLFW_KEY_UP);
        if (fsKey == GLFW_PRESS && prevFSKey == GLFW_RELEASE) {
            if (!isFullscreen) {
                glfwGetWindowPos(window, &windowedX, &windowedY);
                glfwGetWindowSize(window, &windowedW, &windowedH);
                //hacky way to get monitor with most overlap. Needs more testing
                monitor = getCurrentMonitor(window);
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
        prevFSKey = fsKey;
        //check for resize
        int newW, newH;
        glfwGetFramebufferSize(window, &newW, &newH);
        if (newW != w || newH != h) {
            w = newW;
            h = newH;
            glViewport(0, 0, w, h);
            bridge.resize(w, h);
            //need bitset check for h and w vars here, to skip unneeded swaps
            if (presets[activeIdx].spec.fftUsesExprVar[WINDOW_WIDTH] ||
                presets[activeIdx].spec.fftUsesExprVar[WINDOW_HEIGHT] ||
                presets[activeIdx].spec.customFFTSizeScalesWithWindow ||
                presets[activeIdx].spec.feedbackUsesExprVar[WINDOW_WIDTH] ||
                presets[activeIdx].spec.feedbackUsesExprVar[WINDOW_HEIGHT] ||
                presets[activeIdx].spec.feedbackBufferScalesWithWindow) {
                needsSwap = true;
            }
        }
        //check for frame rate change
        monitor = glfwGetWindowMonitor(window);
        if (monitor) {
            mode = glfwGetVideoMode(monitor);
            if (mode && displayHz != mode->refreshRate &&
                (presets[activeIdx].spec.fftUsesExprVar[DISPLAY_HZ] ||
                presets[activeIdx].spec.feedbackUsesExprVar[DISPLAY_HZ])) {
                needsSwap = true;
            }
        }
        // hot reload if necessary
        if (!presets[activeIdx].fragPath.empty()
            && std::filesystem::exists(presets[activeIdx].fragPath)) {
            auto fragTime = std::filesystem::last_write_time(presets[activeIdx].fragPath);
            std::filesystem::file_time_type specTime{};
            if (!presets[activeIdx].specPath.empty() 
                && std::filesystem::exists(presets[activeIdx].specPath)) {
                specTime = std::filesystem::last_write_time(presets[activeIdx].specPath);
            }
            if (fragTime != presets[activeIdx].lastFragWrite
                || specTime != presets[activeIdx].lastSpecWrite) {
                presets[activeIdx].lastFragWrite = fragTime;
                presets[activeIdx].lastSpecWrite = specTime;
                std::cout << "hot reload: " << presets[activeIdx].name << "\n";
                reloadPreset(presets[activeIdx]);
                if (!presets[activeIdx].hasError) {
                    needsSwap = true;
                }
            }
        }
        //do swap if necessary, if eval fails, update activeIndex error msg
        if (needsSwap) {
            if (evalPresetExprs(w, h, displayHz, audio, presets[activeIdx]) &&
                assertUserDefinedBufferSizes(presets[activeIdx])) {
                std::cout << "Swapping to: " << presets[activeIdx].name << "\n";
                std::cout << "Custom FFT size = " <<
                          presets[activeIdx].spec.customFFTSize <<
                          ", Feedback buffer size = " <<
                          presets[activeIdx].spec.feedbackBufferSize << "\n";
                doSwap(activeIdx, presets, bridge, ssbos);
            }
            setTitleBarForPreset(window, activeIdx, presets[activeIdx].name);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        //clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        //either analyze new frame and format, or just temporally move values to send
        if (audio.canAnalyze()) {
            audio.analyze();
            bridge.formatData();
        }
        bridge.nextFrame();
        //write to gpu buffers
        size_t prSize  = bridge.getPeakRMSGPUSizeInBytes();
        size_t fftSize = bridge.getFFTGPUSizeInBytes();
        ssbos[0].write(bridge.getPeakRMSPtr(), prSize);
        ssbos[1].write(bridge.getFFTPtr(), fftSize);
        if (presets[activeIdx].spec.getsPeakRMSHolds) {
            ssbos[2].write(bridge.getPeakRMSHoldPtr(), prSize);
        }
        if (presets[activeIdx].spec.getsFFTHolds) {
            ssbos[3].write(bridge.getFFTHoldPtr(), fftSize);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        //get vars for uniforms
        float t = (float)glfwGetTime();
        size_t bins = bridge.getFFTGPUSize();
        size_t channels = audio.getNumChannels();
        //use shader based on error state
        if (presets[activeIdx].hasError) {
            errorShader.use();
            glUniform1f(errorShader.uniforms.time,        t);
            glUniform1i(errorShader.uniforms.numBins,     bins);
            glUniform1i(errorShader.uniforms.numChannels, channels);
            glUniform1f(errorShader.uniforms.H,           (float)h);
            glUniform1f(errorShader.uniforms.W,           (float)w);
            glUniform1i(errorShader.uniforms.frameCount,  frameCounter);
            glUniform1i(errorShader.uniforms.sampleRate,  sampleRate);
            int chars[128] = {};
            std::string msg = presets[activeIdx].errorMessage;
            int len = std::min((int)msg.size(), 128);
            for (int i = 0; i < len; i++) {
                chars[i] = (int)msg[i];
            }
            glUniform1iv(errorShader.uniforms.errorChars, 128, chars);
            glUniform1i(errorShader.uniforms.errorLen,    len);
            glUniform1i(errorShader.uniforms.showError,   1);
        }
        else {
            presets[activeIdx].shader.use();
            glUniform1f(presets[activeIdx].shader.uniforms.time,        t);
            glUniform1i(presets[activeIdx].shader.uniforms.numBins,     bins);
            glUniform1i(presets[activeIdx].shader.uniforms.numChannels, channels);
            glUniform1f(presets[activeIdx].shader.uniforms.H,           (float)h);
            glUniform1f(presets[activeIdx].shader.uniforms.W,           (float)w);
            glUniform1i(presets[activeIdx].shader.uniforms.frameCount,  frameCounter);
            glUniform1i(presets[activeIdx].shader.uniforms.sampleRate,  sampleRate);
            glUniform1i(presets[activeIdx].shader.uniforms.showError,   0);
            glUniform1i(presets[activeIdx].shader.uniforms.errorLen,    0);
            bindTextures(presets[activeIdx]);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        //flip for user defined feedback ssbo
        feedbackFlip = !feedbackFlip;
        ssbos[4].bind(feedbackFlip ? 4 : 5);
        ssbos[5].bind(feedbackFlip ? 5 : 4);
        //not sure if necessary
        unbindTextures(presets[activeIdx]);
        //set swap and count frame counter
        glfwSwapBuffers(window);
        frameCounter = (++frameCounter) % displayHz;
    }
    //free everything and shut down
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout.rdbuf(origCout);
    std::cerr.rdbuf(origCerr);
    logFile.close();
    return 0;
}

