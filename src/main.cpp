#define MINIAUDIO_IMPLEMENTATION

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "audio/audio_system.hpp"
#include "gpu/shader_system.hpp"
#include "utils/glfw_context.hpp"
#include "utils/input_handler.hpp"
#include "gpu/gpu_buffers.hpp"
#include "utils/log.hpp"

std::string getAssetPath(const std::string& relative) {
#ifdef _WIN32
    char buf[4096];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len == 0 || len == sizeof(buf)) return relative;
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return relative;
    buf[len] = '\0';
    auto binDir = std::filesystem::path(buf).parent_path();
    return (binDir / relative).string();
#endif
}

void evalPresetExprs(Globals& g, ShaderPreset* pre) {
    ExprContext ctx;
    ctx.windowWidth   = g.W;
    ctx.windowHeight  = g.H;
    ctx.numChannels   = g.numChannels;
    ctx.displayHz     = g.displayHz;
    ctx.sampleRate    = g.sampleRate;
    ctx.fftSize       = g.fftSize;
    ctx.fftBinAmt     = g.fftBinAmt;
    ctx.fftArrSize    = g.fftArrSize;
    ctx.hopSize       = g.hopSize;
    ctx.hopAmount     = g.hopAmt;
    ctx.isFeedbackExpr = false;
    std::string ret  = evalSpecExprs(pre->spec, ctx);
    if (!ret.empty()) {
        pre->errorMessage = formatErrorMessageForPreset(ret, pre->errorLen);
        pre->hasError = true;
    }
}

ResizeValues getResizeValues(Globals& gl, AudioSystem& a, ShaderSystem& s) {
    ResizeValues r;
    r.prSize = a.bridge.getPeakRMSGPUSizeInBytes();
    r.fftSize = gl.fftArrSize * sizeof(float);
    r.fbSize = gl.getSizeFromModeSwitch(s.active->spec.feedbackBufferSize
                                        * sizeof(float),
                                        s.active->spec.feedbackBufferScalesWithWindow);
    r.fbInit = s.active->spec.feedbackBufferInitValue;
    r.prHSize = (s.active->spec.getPeakRMSHolds) ? r.prSize : 0;
    r.fftHSize = (s.active->spec.getFFTHolds) ? r.fftSize : 0;
    size_t samp = (s.active->spec.isRawSamplesMono) ? gl.hopSize :
                                                      gl.hopSize * gl.numChannels;
    r.rawSize = (s.active->spec.getRawSamples) ? samp * sizeof(float) : 0;
    return r;
}

void doSwap(ShaderSystem& s, AudioSystem& a, GPUBuffers& g,
            Globals& gl, size_t fbMax) {
    std::cout << "Swapping to: " << s.active->name << "\n";
    a.updateAudioGlobals(s.active->spec);
    evalPresetExprs(gl, s.active);
    assertUserDefinedBufferSizes(s.active, fbMax);
    validateFFTRates(gl, s.active);
    a.swap(s.active->spec);
    ResizeValues r = getResizeValues(gl, a, s);
    g.swap(r);
    s.errorSwap();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

int main() {
    //init log file writer
    Log log(getAssetPath("log.txt"));
    //init glfw, monitor, window, mode, and displayHz
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return -1; }
    Globals globals;
    GLFWContext glfw(globals);
    if (!glfw.isValid()) return -1;
    //init glad (handled by gl_platform.hpp)
    if (!initGLAD()) return -1;
    //get initial width and height from framebuffer then log gl info
    glfw.initFramebuffer();
    globals.initWidth = globals.W; globals.initHeight = globals.H;
    size_t maxFBBufferFloats = glfw.logGLInfo() / sizeof(float);
    //set max feedback buffer size to the lower of a 4k framebuffer or hardware limit
    maxFBBufferFloats = std::min(maxFBBufferFloats, (size_t)33177600);
    //init shaders
    ShaderSystem shaders(getAssetPath("shaders/"), globals);
    if (!shaders.isValid()) return -1;
    //pi only
    //fftwf_import_wisdom_from_filename(getAssetPath("fftw_wisdom.dat").c_str());
    //init audio
    AudioSystem audioSys(globals, shaders.active->spec);
    if (!audioSys.isValid()) return -1;
    //init gpu verts and buffers
    GPUBuffers gpuBuffs(shaders.active->spec.feedbackBufferInitValue);
    //swap all configs to first preset
    doSwap(shaders, audioSys, gpuBuffs, globals, maxFBBufferFloats);
    glfw.setTitleBarForPreset(shaders.getIndex(), shaders.active->name);
    //catches button presses and handles them
    InputHandler input(globals, shaders, audioSys);
    //per frame loop
    while (!glfwWindowShouldClose(glfw.window)) {
        //flag for swap
        bool needsSwap = false;
        //poll for input and handle it
        input.handleInput(glfw, needsSwap);
        //check for resize
        glfw.checkForResize(audioSys, shaders.active, needsSwap);
        //check for frame rate change
        glfw.checkForFrameRateChange(shaders.active, needsSwap);
        //check for hot reload
        shaders.hotReloadCheck(needsSwap);
        //do swap if necessary, if eval fails, update active's error msg
        if (needsSwap) {
            doSwap(shaders, audioSys, gpuBuffs, globals, maxFBBufferFloats);
            glfw.setTitleBarForPreset(shaders.getIndex(), shaders.active->name);
        }
        //clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        //either analyze new frame and format, or just temporally move values to send
        globals.newAudioWindow = audioSys.analyzeAndFormat();
        globals.time = glfwGetTime();
        //write to gpu buffers
        gpuBuffs.writeToBuffers(audioSys.bridge, shaders.active->spec, globals);
        //use shader based on error state
        if (globals.showDeviceMenu) { shaders.useDeviceMenuShader(); }
        else if (shaders.active->hasError) { shaders.useErrorShader(); }
        else { shaders.useActiveShader(); }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        //flip for user defined feedback ssbo
        gpuBuffs.flipFeedback();
        //not sure if necessary
        unbindTextures(shaders.active);
        //set swap and count frame counter
        glfwSwapBuffers(glfw.window);
    }
    //fftwf_export_wisdom_to_filename(getAssetPath("fftw_wisdom.dat").c_str());
    std::cout << "Program ended successfully :)";
    return 0;
}
