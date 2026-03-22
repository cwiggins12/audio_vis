#define MINIAUDIO_IMPLEMENTATION

#include "audio.hpp"
#include "av_bridge.hpp"
#include "shader_loader.hpp"
#include <GLFW/glfw3.h>

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
std::time_t t = std::chrono::system_clock::to_time_t(now);
std::cout << "=== audio_vis started: " << std::ctime(&t);

    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    //this will change eventually
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

    //ties gl frames to device fps
    glfwSwapInterval(1);

    //keep this around for Raspberry Pi testing l8r
    std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) 
              << std::endl;

    //may wanna add some dynamic hopAmt or fftOrder changes to account for other rates
    int displayHz = 60; //fallback :(
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
        std::cerr << "Audio initialization failed \n";
        glfwDestroyWindow(window);
        glfwTerminate();
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
    bindSSBO(0, bridge.getPeakRMSGPUSizeInBytes(),  ssbos[0]);
    bindSSBO(1, bridge.getFFTGPUSizeInBytes(),      ssbos[1]);
    bindSSBO(2, bridge.getPeakRMSGPUSizeInBytes(),  ssbos[2]);
    bindSSBO(3, bridge.getFFTGPUSizeInBytes(),      ssbos[3]);
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
            bindSSBO(0, bridge.getPeakRMSGPUSizeInBytes(), ssbos[0]);
            bindSSBO(1, bridge.getFFTGPUSizeInBytes(),     ssbos[1]);
            bindSSBO(2, bridge.getPeakRMSGPUSizeInBytes(), ssbos[2]);
            bindSSBO(3, bridge.getFFTGPUSizeInBytes(),     ssbos[3]);
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
        float t = (float)glfwGetTime();
        glUniform1f(presets[activeIdx].shader.uniforms["time"], t);
        glUniform1i(presets[activeIdx].shader.uniforms["numBins"],
                    bridge.getFFTGPUSize());
        glUniform1i(presets[activeIdx].shader.uniforms["numChannels"],
                    audio.getNumChannels());
        glUniform1f(presets[activeIdx].shader.uniforms["H"], h);
        glUniform1f(presets[activeIdx].shader.uniforms["W"], w);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        //this blocks until next vblank and makes the loop fire once per device frame
        glfwSwapBuffers(window);
    }
    glDeleteBuffers(4, ssbos);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout.rdbuf(origCout);
    std::cerr.rdbuf(origCerr);
    logFile.close();
    return 0;
}

