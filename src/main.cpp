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
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "audio_vis", nullptr, nullptr);
    if (!window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);

    glfwSwapInterval(1);

    std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    int displayHz = 60;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode) displayHz = mode->refreshRate;

    std::vector<ShaderPreset> presets = loadPresets(getAssetPath("shaders/"));
    if (presets.empty()) {
        std::cerr << "No valid presets found\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    int activeIdx = 0;

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

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    SSBO ssbos[4];
    ssbos[0].alloc(bridge.getPeakRMSGPUSizeInBytes());      ssbos[0].bind(0);
    ssbos[1].alloc(bridge.getFFTGPUSizeInBytes());          ssbos[1].bind(1);
    ssbos[2].alloc(bridge.getPeakRMSGPUSizeInBytes());      ssbos[2].bind(2);
    ssbos[3].alloc(bridge.getFFTGPUSizeInBytes());          ssbos[3].bind(3);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    int prevRightKey = GLFW_RELEASE;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int rightKey = glfwGetKey(window, GLFW_KEY_RIGHT);
        if (rightKey == GLFW_PRESS && prevRightKey == GLFW_RELEASE) {
            activeIdx = (activeIdx + 1) % presets.size();
            bridge.swapSpec(presets[activeIdx].spec);
            ssbos[0].resize(bridge.getPeakRMSGPUSizeInBytes());     ssbos[0].bind(0);
            ssbos[1].resize(bridge.getFFTGPUSizeInBytes());         ssbos[1].bind(1);
            ssbos[2].resize(bridge.getPeakRMSGPUSizeInBytes());     ssbos[2].bind(2);
            ssbos[3].resize(bridge.getFFTGPUSizeInBytes());         ssbos[3].bind(3);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        prevRightKey = rightKey;

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

        size_t prSize  = bridge.getPeakRMSGPUSizeInBytes();
        size_t fftSize = bridge.getFFTGPUSizeInBytes();
        ssbos[0].write(bridge.getPeakRMSPtr(),  prSize);
        ssbos[1].write(bridge.getFFTPtr(),      fftSize);
        if (presets[activeIdx].spec.getsPeakRMSHolds) {
            ssbos[2].write(bridge.getPeakRMSHoldPtr(), prSize);
        }
        if (presets[activeIdx].spec.getsFFTHolds) {
            ssbos[3].write(bridge.getFFTHoldPtr(),     fftSize);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        presets[activeIdx].shader.use();
        float t = (float)glfwGetTime();
        glUniform1f(presets[activeIdx].uniforms.time, t);
        glUniform1i(presets[activeIdx].uniforms.numBins, bridge.getFFTGPUSize());
        glUniform1i(presets[activeIdx].uniforms.numChannels, audio.getNumChannels());
        glUniform1f(presets[activeIdx].uniforms.H, (float)h);
        glUniform1f(presets[activeIdx].uniforms.W, (float)w);
        glDrawArrays(GL_TRIANGLES, 0, 3);

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

