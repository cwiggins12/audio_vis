#pragma once

#include "shader_loader.hpp"
#include "audio_system.hpp"

struct ShaderSystem {
public:
    ShaderPreset*             active = nullptr;

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

    void useErrorShader(int w, int h) {
        error.use();
        int chars[128] = {};
        std::string msg = active->errorMessage;
        int len = std::min((int)msg.size(), 128);
        for (int i = 0; i < len; i++) {
            chars[i] = (int)msg[i];
        }
        glUniform1f(error.uniforms[U_W], (float)w);
        glUniform1f(error.uniforms[U_H], (float)h);
        glUniform1i(error.uniforms[U_ERROR_LEN], len);
        glUniform1i(error.uniforms[U_SHOW_ERROR], 1);
        glUniform1iv(error.uniforms[U_ERROR_CHARS], 128, chars);
    }

    void useActiveShader(float t, AudioSystem& a, int h, int w,
                         bool newAudioWindow, int hz) {
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

    void hotReloadCheck(bool& needsSwap) {
        auto fragPath = std::filesystem::path(active->shaderDir) / "frag.glsl";
        auto specPath = std::filesystem::path(active->shaderDir) / "spec.cfg";
        if (!fragPath.empty()
            && std::filesystem::exists(fragPath)) {
            auto fragTime = std::filesystem::last_write_time(fragPath);
            std::filesystem::file_time_type specTime{};
            if (!specPath.empty() 
                && std::filesystem::exists(specPath)) {
                specTime = std::filesystem::last_write_time(specPath);
            }
            if (fragTime != active->lastFragWrite
                || specTime != active->lastSpecWrite) {
                active->lastFragWrite = fragTime;
                active->lastSpecWrite = specTime;
                std::cout << "hot reload: " << active->name << "\n";
                reloadPreset(active);
                if (!active->hasError) {
                    needsSwap = true;
                }
            }
        }
    }
private:
    std::vector<ShaderPreset> presets;
    Shader                    error;
    int                       index = 0;
};

