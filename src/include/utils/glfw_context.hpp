#pragma once

#include "utils/gl_platform.hpp"
#include <iostream>
#include "gpu/shader_preset.hpp"
#include "audio/audio_system.hpp"
#include "config/globals.hpp"

struct GLFWContext {
public:
    GLFWwindow*        window    = nullptr;

    GLFWContext(Globals& g) : globals(g) {
        monitor = glfwGetPrimaryMonitor();
        mode = glfwGetVideoMode(monitor);
        if (!mode) {
            std::cerr << "Unable to get glfw vidmode\n";
            return;
        }
        g.displayHz = mode->refreshRate;
        setGLFWContextHints(mode);
        window = glfwCreateWindow(mode->width, mode->height,
                 "audio_vis", nullptr, nullptr);
        if (!window) {
            std::cerr << "glfwCreateWindow failed\n";
            return;
        }
        glfwMakeContextCurrent(window);
        glfwGetWindowPos(window, &windowedX, &windowedY);
    }

    ~GLFWContext() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }

    bool isValid() {
        return (window && mode);
    }

    void initFramebuffer() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        globals.W = w;          globals.H = h;
        windowedW = globals.W;  windowedH = globals.H;
        pendingW  = globals.W;  pendingH  = globals.H;
        glViewport(0, 0, globals.W, globals.H);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int w, int h) {
            auto* ctx = static_cast<GLFWContext*>(glfwGetWindowUserPointer(win));
            ctx->pendingW = w;
            ctx->pendingH = h;
            ctx->resizePending = true;
        });
    }

    size_t logGLInfo() {
        //print info to log
        std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
        std::cout << "GLSL Version: " <<
                     glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
        std::cout << "Found device frame rate: " << globals.displayHz << std::endl;

        GLint maxBinds = 0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxBinds);
        std::cout << "Max SSBO bindings: " << maxBinds << "\n";
        GLint maxBlock = 0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxBlock);
        std::cout << "Max SSBO block size: " << maxBlock << " bytes\n";

        if (maxBinds < 6) {
            std::cerr << "WARNING: GPU only supports " << maxBinds
                      << " SSBO bindings, but audio_vis requires 6. "
                      << "Feedback buffers may not work.\n";
        }
        return (size_t)maxBlock;
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
        glfwSwapInterval(1);
    }

    void checkForResize(AudioSystem& a, ShaderPreset* p, bool& needsSwap) {
        if (!resizePending) return;
        resizePending = false;
        globals.W = pendingW;
        globals.H = pendingH;
        glViewport(0, 0, globals.W, globals.H);
        if (p->spec.fftUsesExprVar[WINDOW_WIDTH] ||
            p->spec.fftUsesExprVar[WINDOW_HEIGHT] ||
            p->spec.customFFTSizeScalesWithWindow ||
            p->spec.feedbackUsesExprVar[WINDOW_WIDTH] ||
            p->spec.feedbackUsesExprVar[WINDOW_HEIGHT] ||
            p->spec.feedbackBufferScalesWithWindow) {
            needsSwap = true;
        }
    }

    void checkForFrameRateChange(ShaderPreset* p, bool& needsSwap) {
        monitor = glfwGetWindowMonitor(window);
        if (monitor) {
            mode = glfwGetVideoMode(monitor);
            if (mode && globals.displayHz != mode->refreshRate &&
                (p->spec.fftUsesExprVar[DISPLAY_HZ] ||
                p->spec.feedbackUsesExprVar[DISPLAY_HZ])) {
                globals.displayHz = mode->refreshRate;
                needsSwap = true;
            }
        }
    }

    void setTitleBarForPreset(int i, std::string& s) {
        std::string newTitle = "audio_vis - Preset " + std::to_string(i) + ": " + s;
        glfwSetWindowTitle(window, newTitle.c_str());
    }

private:
    GLFWmonitor*       monitor   = nullptr;
    const GLFWvidmode* mode      = nullptr;
    int windowedX = 0, windowedY = 0;
    int windowedW = 0, windowedH = 0;
    int pendingW  = 0, pendingH  = 0;
    bool resizePending = false;
    bool isFullscreen  = false;
    Globals& globals;

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
