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

//h8 this, but there is no current glfw ops to get the monitor the window is most on
//almost certain this among other related things will break on Wayland,
//since it, to my knowledge, doesn't expose window positioning
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

void uploadError(const ShaderPreset& p, const std::string& msg) {
    int chars[128] = {};
    int len = std::min((int)msg.size(), 128);
    for (int i = 0; i < len; i++)
        chars[i] = (int)msg[i];
    glUniform1iv(p.shader.uniforms.errorChars, 128, chars);
    glUniform1i(p.shader.uniforms.errorLen,   len);
    glUniform1i(p.shader.uniforms.showError,  1);
}

void doSwap(int activeIdx, std::vector<ShaderPreset>& presets,
            AVBridge& bridge, SSBO* ssbos) {
    bridge.swapSpec(presets[activeIdx].spec);
    ssbos[0].resize(bridge.getPeakRMSGPUSizeInBytes()); ssbos[0].bind(0);
    ssbos[1].resize(bridge.getFFTGPUSizeInBytes());     ssbos[1].bind(1);
    ssbos[2].resize(bridge.getPeakRMSGPUSizeInBytes()); ssbos[2].bind(2);
    ssbos[3].resize(bridge.getFFTGPUSizeInBytes());     ssbos[3].bind(3);
    size_t fbSize = presets[activeIdx].spec.feedbackBufferSize * sizeof(float);
    float fbInit = presets[activeIdx].spec.feedbackBufferInitValue;
    ssbos[4].resize(fbSize);    ssbos[4].fill(fbInit);  ssbos[4].bind(4);
    ssbos[5].resize(fbSize);    ssbos[5].fill(fbInit);  ssbos[5].bind(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

int main() {
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

    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate(); 
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
        return -1;
    }

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glfwSwapInterval(1);

    std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

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
    int activeIdx = 0;

    //set up a displayhz switch to ensure these are good choices based on that
    const int fft_order = 13;
    const int hopAmt = 4;

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

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    SSBO ssbos[6];
    ssbos[0].alloc(bridge.getPeakRMSGPUSizeInBytes());          ssbos[0].bind(0);
    ssbos[1].alloc(bridge.getFFTGPUSizeInBytes());              ssbos[1].bind(1);
    ssbos[2].alloc(bridge.getPeakRMSGPUSizeInBytes());          ssbos[2].bind(2);
    ssbos[3].alloc(bridge.getFFTGPUSizeInBytes());              ssbos[3].bind(3);
    size_t fbSize = presets[0].spec.feedbackBufferSize * sizeof(float);
    float fbInitVal = presets[0].spec.feedbackBufferInitValue;
    ssbos[4].alloc(fbSize);     ssbos[4].fill(fbInitVal);       ssbos[4].bind(4);
    ssbos[5].alloc(fbSize);     ssbos[5].fill(fbInitVal);       ssbos[5].bind(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    int windowedX = 0, windowedY = 0, windowedW = w, windowedH = h;
    glfwGetWindowPos(window, &windowedX, &windowedY);

    int prevRightKey = GLFW_RELEASE;
    int prevLeftKey = GLFW_RELEASE;
    int prevFSKey = GLFW_RELEASE;
    bool isFullscreen = false;
    bool feedbackFlip = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int rightKey = glfwGetKey(window, GLFW_KEY_RIGHT);
        if (rightKey == GLFW_PRESS && prevRightKey == GLFW_RELEASE) {
            activeIdx = (activeIdx + 1) % presets.size();
            doSwap(activeIdx, presets, bridge, ssbos);
            setTitleBarForPreset(window, activeIdx, presets[activeIdx].name);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        prevRightKey = rightKey;

        int leftKey = glfwGetKey(window, GLFW_KEY_LEFT);
        if (leftKey == GLFW_PRESS && prevLeftKey == GLFW_RELEASE) {
            activeIdx = ((activeIdx - 1) + (int)presets.size()) % (int)presets.size();
            doSwap(activeIdx, presets, bridge, ssbos);
            setTitleBarForPreset(window, activeIdx, presets[activeIdx].name);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        prevLeftKey = leftKey;

        int fsKey = glfwGetKey(window, GLFW_KEY_UP);
        if (fsKey == GLFW_PRESS && prevFSKey == GLFW_RELEASE) {
            if (!isFullscreen) {
                glfwGetWindowPos(window, &windowedX, &windowedY);
                glfwGetWindowSize(window, &windowedW, &windowedH);
                //hacky way to get monitor with most overlap. Needs more testing
                GLFWmonitor* mon = getCurrentMonitor(window);
                const GLFWvidmode* vm = glfwGetVideoMode(mon);
                glfwSetWindowMonitor(window, mon, 0, 0,
                                     vm->width, vm->height, vm->refreshRate);
            }
            else {
                glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, 
                                     windowedW, windowedH, 0);
            }
            isFullscreen = !isFullscreen;
        }
        prevFSKey = fsKey;

        int newW, newH;
        glfwGetFramebufferSize(window, &newW, &newH);
        if (newW != w || newH != h) {
            w = newW;
            h = newH;
            glViewport(0, 0, w, h);
            bridge.resize(w, h);
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (audio.canAnalyze()) {
            audio.analyze();
            bridge.formatData();
        }
        bridge.nextFrame();

        // hot reload
        auto& active = presets[activeIdx];
        auto fragTime = std::filesystem::last_write_time(active.fragPath);
        std::filesystem::file_time_type specTime{};
        if (!active.specPath.empty() && std::filesystem::exists(active.specPath))
            specTime = std::filesystem::last_write_time(active.specPath);

        if (fragTime != active.lastFragWrite || specTime != active.lastSpecWrite) {
            active.lastFragWrite = fragTime;
            active.lastSpecWrite = specTime;
            std::cout << "hot reload: " << active.name << "\n";
            reloadPreset(active);
            if (!active.hasError)
                doSwap(activeIdx, presets, bridge, ssbos);
        }

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

        float t = (float)glfwGetTime();
        size_t bins = bridge.getFFTGPUSize();
        size_t channels = audio.getNumChannels();

        if (presets[activeIdx].hasError) {
            getErrorShader().use();
            glUniform1f(getErrorShader().uniforms.time,        t);
            glUniform1i(getErrorShader().uniforms.numBins,     bins);
            glUniform1i(getErrorShader().uniforms.numChannels, channels);
            glUniform1f(getErrorShader().uniforms.H,           (float)h);
            glUniform1f(getErrorShader().uniforms.W,           (float)w);
            uploadError(presets[activeIdx], presets[activeIdx].errorMessage);
        } else {
            presets[activeIdx].shader.use();
            glUniform1f(presets[activeIdx].shader.uniforms.time,        t);
            glUniform1i(presets[activeIdx].shader.uniforms.numBins,     bins);
            glUniform1i(presets[activeIdx].shader.uniforms.numChannels, channels);
            glUniform1f(presets[activeIdx].shader.uniforms.H,           (float)h);
            glUniform1f(presets[activeIdx].shader.uniforms.W,           (float)w);
            glUniform1i(presets[activeIdx].shader.uniforms.showError,   0);
            glUniform1i(presets[activeIdx].shader.uniforms.errorLen,    0);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        feedbackFlip = !feedbackFlip;
        ssbos[4].bind(feedbackFlip ? 4 : 5);
        ssbos[5].bind(feedbackFlip ? 5 : 4);

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout.rdbuf(origCout);
    std::cerr.rdbuf(origCerr);
    logFile.close();
    return 0;
}

