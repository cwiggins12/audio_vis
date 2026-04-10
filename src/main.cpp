#define MINIAUDIO_IMPLEMENTATION

#include "audio/audio_system.hpp"
#include "gpu/shader_system.hpp"
#include "utils/glfw_context.hpp"
#include "utils/input_handler.hpp"
#include "gpu/gpu_buffers.hpp"
#include "utils/log.hpp"

std::string getAssetPath(const std::string& relative) {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return relative;
    buf[len] = '\0';
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
}

void evalPresetExprs(Globals& g, ShaderPreset* pre) {
    ExprContext ctx;
    ctx.windowWidth  = g.W;
    ctx.windowHeight = g.H;
    ctx.numChannels  = g.numChannels;
    ctx.displayHz    = g.displayHz;
    ctx.sampleRate   = g.sampleRate;
    ctx.fftSize      = g.fftSize;
    ctx.fftBinAmt    = g.fftBinAmt;
    std::string ret = evalSpecExprs(pre->spec, ctx);
    if (!ret.empty()) {
        pre->errorMessage = ret;
        pre->hasError = true;
    }
}

void doSwap(ShaderPreset* p, AudioSystem& a, GPUBuffers& g) {
    a.swap(p->spec);
    ResizeValues r;
    r.prSize = a.bridge.getPeakRMSGPUSizeInBytes();
    r.fftSize = a.bridge.getFFTGPUSizeInBytes();
    r.fbSize = a.bridge.getSizeFromModeSwitch(
                        p->spec.feedbackBufferSize * sizeof(float),
                        p->spec.feedbackBufferScalesWithWindow);
    r.fbInit = p->spec.feedbackBufferInitValue;
    r.prHSize = (p->spec.getPeakRMSHolds) ? r.prSize : 0;
    r.fftHSize = (p->spec.getFFTHolds) ? r.fftSize : 0;
    r.hopSize = (p->spec.getRawSamples) ? a.bridge.getHopSizeInBytes() : 0;
    g.swap(r);
}

int main() {
    //init log file writer
    Log log(getAssetPath("log.txt"));
    //init glfw, monitor, window, mode, and displayHz
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return -1; }
    Globals globals;
    GLFWContext glfw(globals);
    if (!glfw.isValid()) return -1;
    //init glad
    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }
    //get initial width and height from framebuffer then log gl info
    //int w, h;
    glfw.initFramebuffer(globals.W, globals.H);
    //globals.H = h; globals.W = w;
    size_t maxFBBufferFloats = glfw.logGLInfo() / sizeof(float);
    //set max feedback buffer size to the lower of a 4k framebuffer or hardware limit
    maxFBBufferFloats = std::min(maxFBBufferFloats, (size_t)33177600);
    //init shaders
    ShaderSystem shaders(getAssetPath("shaders/"));
    if (!shaders.isValid()) return -1;
    //init audio
    AudioSystem audioSys(globals, shaders.active->spec);
    if (!audioSys.isValid()) return -1;
    glfw.setTitleBarForPreset(shaders.getIndex(), shaders.active->name);
    //init gpu verts and buffers
    GPUBuffers gpuBuffs(shaders.active->spec.feedbackBufferInitValue);
    //swap all configs to first preset, unless eval error, then use errorShader
    evalPresetExprs(globals, shaders.active);
    assertUserDefinedBufferSizes(shaders.active, maxFBBufferFloats);
    doSwap(shaders.active, audioSys, gpuBuffs);
    //catches button presses and handles them
    InputHandler input;
    //per frame loop
    while (!glfwWindowShouldClose(glfw.window)) {
        //flag for swap
        bool needsSwap = false;
        //poll for input and handle it
        input.handleInput(glfw, shaders, needsSwap);
        //check for resize
        glfw.checkForResize(audioSys, shaders.active, globals.W, globals.H, needsSwap);
        //check for frame rate change
        glfw.checkForFrameRateChange(shaders.active, needsSwap);
        //check for hot reload
        shaders.hotReloadCheck(needsSwap);
        //do swap if necessary, if eval fails, update active's error msg
        if (needsSwap) {
            evalPresetExprs(globals, shaders.active);
            assertUserDefinedBufferSizes(shaders.active, maxFBBufferFloats);
            std::cout << "Swapping to: " << shaders.active->name << "\n";
            doSwap(shaders.active, audioSys, gpuBuffs);
            glfw.setTitleBarForPreset(shaders.getIndex(), shaders.active->name);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        //clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        //either analyze new frame and format, or just temporally move values to send
        globals.newAudioWindow = audioSys.analyzeAndFormat();
        //write to gpu buffers
        globals.time = glfwGetTime();
        gpuBuffs.writeToBuffers(audioSys.bridge, shaders.active->spec, globals);
        //use shader based on error state
        if (shaders.active->hasError) {
            shaders.useErrorShader(globals.W, globals.H);
        }
        else {
            shaders.useActiveShader((float)glfwGetTime(), audioSys,
                                    globals.H, globals.W, globals.newAudioWindow, globals.displayHz);
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
    std::cout << "Program ended successfully :)";
    return 0;
}

