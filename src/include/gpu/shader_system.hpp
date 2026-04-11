#pragma once

#include "gpu/shader_loader.hpp"

struct ShaderSystem {
public:
    ShaderPreset* active = nullptr;

    ShaderSystem(const std::string& shaderPath, Globals& g) : globals(g) {
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
    }

    bool isValid() {
        return active != nullptr;
    }

    void setIndex(int i) {
        index  = i;
        active = &presets[index];
    }

    int getIndex() {
        return index;
    }

    int getSize() {
        return presets.size();
    }

    void useErrorShader() {
        error.use();
    }

    void useActiveShader() {
        active->shader.use();
        bindTextures(active);
    }

    void hotReloadCheck(bool& needsSwap) {
        auto fragPath = std::filesystem::path(active->shaderDir) / "frag.glsl";
        auto specPath = std::filesystem::path(active->shaderDir) / "spec.cfg";
        if (!fragPath.empty() && std::filesystem::exists(fragPath)) {
            auto fragTime = std::filesystem::last_write_time(fragPath);
            std::filesystem::file_time_type specTime{};
            if (!specPath.empty() && std::filesystem::exists(specPath)) {
                specTime = std::filesystem::last_write_time(specPath);
            }
            if (fragTime != active->lastFragWrite ||
                     specTime != active->lastSpecWrite) {
                active->lastFragWrite = fragTime;
                active->lastSpecWrite = specTime;
                std::cout << "Hot Reloading: " << active->name << "\n";
                reloadPreset(active);
                if (!active->hasError) {
                    needsSwap = true;
                }
            }
        }
        else {
            if (std::filesystem::exists(active->shaderDir)) {
                active->hasError = true;
                const std::string err = active->name + " error - Hot Reload. " +
                                        "frag.glsl could not be found on hot reload check.";
                active->errorMessage = formatErrorMessageForPreset(err, active->errorLen);
            }
            else {
                removeActiveFromPresets();
            }
        }
    }

    void removeActiveFromPresets() {
        std::string removedName = active->name;
        // Don't remove the last preset: fall back to error state instead
        if (presets.size() <= 1) {
            active->hasError = true;
            const std::string err = removedName +
                                    " - shader directory was deleted. No other presets available.";
            active->errorMessage = formatErrorMessageForPreset(err, active->errorLen);
            std::cerr << err << "\n";
            return;
        }
        active->destroyTextures();
        presets.erase(presets.begin() + index);
        // Clamp index into the now-smaller vector
        if (index >= (int)presets.size()) {
            index = (int)presets.size() - 1;
        }
        active = &presets[index];
        active->hasError = true;
        const std::string err = removedName +
            " was removed. Moving to: " + active->name;
        active->errorMessage = formatErrorMessageForPreset(err, active->errorLen);
        std::cerr << err << "\n";
    }

    void swap() {
        globals.showError  = active->hasError;
        globals.errorLen   = active->errorLen;
        globals.errorChars = active->errorMessage;
    }

private:
    std::vector<ShaderPreset> presets;
    Shader                    error;
    int                       index = 0;
    Globals&                  globals;
};

