#pragma once

#include "utils/glfw_context.hpp"
#include "gpu/shader_system.hpp"

struct InputHandler {
public:
    InputHandler() {
        srand(time(nullptr));
    }

    void handleInput(GLFWContext& glfw, ShaderSystem& s, bool& needsSwap) {
        glfwPollEvents();
        int presetsSize = s.getSize();
        //check for escape press
        if (glfwGetKey(glfw.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(glfw.window, GLFW_TRUE);
        }
        //check forward key
        int rightKey = glfwGetKey(glfw.window, GLFW_KEY_RIGHT);
        if (rightKey == GLFW_PRESS && prevRightKey == GLFW_RELEASE) {
            s.setIndex((s.getIndex() + 1) % presetsSize);
            needsSwap = true;
        }
        prevRightKey = rightKey;
        //check back key
        int leftKey = glfwGetKey(glfw.window, GLFW_KEY_LEFT);
        if (leftKey == GLFW_PRESS && prevLeftKey == GLFW_RELEASE) {
            s.setIndex(((s.getIndex() - 1) + presetsSize) % presetsSize);
            needsSwap = true;
        }
        prevLeftKey = leftKey;
        //check up key for fullscreen
        int fsKey = glfwGetKey(glfw.window, GLFW_KEY_UP);
        if (fsKey == GLFW_PRESS && prevFSKey == GLFW_RELEASE) {
            glfw.toggleFullscreen();
        }
        prevFSKey = fsKey;
        //check for shuffle button
        int shuffleKey = glfwGetKey(glfw.window, GLFW_KEY_DOWN);
        if (shuffleKey == GLFW_PRESS && prevShuffleKey == GLFW_RELEASE) {
            s.setIndex(rand() % presetsSize);
            needsSwap = true;
        }
        prevShuffleKey = shuffleKey;
    }

private:
    int prevRightKey    = GLFW_RELEASE;
    int prevLeftKey     = GLFW_RELEASE;
    int prevFSKey       = GLFW_RELEASE;
    int prevShuffleKey  = GLFW_RELEASE;
    int presetsSize     = 0;
};

