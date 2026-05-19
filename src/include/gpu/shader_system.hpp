#pragma once

#include "gpu/shader_loader.hpp"
#include "utils/shader_watcher.hpp"

struct ShaderSystem {
public:
    ShaderPreset* active = nullptr;

    ShaderSystem(const std::string& shaderPath, Globals& g) : globals(g) {
        shadersPath = shaderPath;
        presets = loadPresets(shaderPath);
        if (presets.empty()) {
            std::cerr << "No valid presets found\n";
            return;
        }
        std::string vtxSrc = getVertexSrc();
        if (!error.init(vtxSrc.c_str(), errorFragSrc).empty()) {
            std::cout << "Failed compilation of error shader. "
                         "Hot reloads will be UB until errorFragSrc is fixed\n";
        }
        if (!deviceMenu.init(vtxSrc.c_str(), deviceFragSrc).empty()) {
            std::cout << "Failed compilation of display menu shader. "
                         "Opening the device menu will be UB until deviceFragSrc is fixed\n";
        }
        active = &presets[0];
        watcher.init(shaderPath,
                     active->shaderDir,
                     getTextureFilenames(active),
                     getFontFilenames(active));
    }

    ~ShaderSystem() {
        watcher.stop();
    }

    bool isValid() {
        return active != nullptr;
    }

    void setIndex(int i) {
        index  = i;
        active = &presets[index];
        updateWatcher();
    }

    int getIndex() { return index; }
    int getSize()  { return (int)presets.size(); }

    void useErrorShader()      { error.use(); }
    void useDeviceMenuShader() { deviceMenu.use(); }

    void useActiveShader() {
        active->shader.use();
        bindTextures(active);
        bindFonts(active);
    }

    void hotReloadCheck(bool& needsSwap) {
        pendingEvents.clear();
        watcher.drain(pendingEvents);

        for (auto& ev : pendingEvents) {
            switch (ev.type) {

            case WatchEventType::SHADER_CHANGED: {
                auto fragPath = std::filesystem::path(active->shaderDir) / "frag.glsl";
                if (!std::filesystem::exists(fragPath)) {
                    if (std::filesystem::exists(active->shaderDir)) {
                        active->hasError = true;
                        const std::string err = "hotReload: " + active->name +
                            "frag.glsl could not be found";
                        active->errorMessage = formatErrorMessageForPreset(
                            err, active->errorLen);
                        std::cerr << err << "\n";
                    } else {
                        removeActiveFromPresets();
                    }
                    errorSwap();
                    break;
                }
                reloadPreset(active);
                if (!active->hasError) {
                    needsSwap = true;
                    updateWatcher(); // spec may have changed texture/font list
                } else {
                    errorSwap();
                }
                break;
            }

            case WatchEventType::TEXTURE_CHANGED: {
                std::string changedFile =
                    std::filesystem::path(ev.path).filename().string();
                for (auto& slot : active->textures) {
                    if (slot.filename == changedFile) {
                        std::cout << "Hot reloading texture: " << active->name << " - " << changedFile << "\n";
                        reloadTextureSlot(active, slot);
                        break;
                    }
                }
                break;
            }

            case WatchEventType::FONT_CHANGED: {
                std::string changedFile =
                    std::filesystem::path(ev.path).filename().string();
                for (auto& slot : active->fonts) {
                    if (slot.filename == changedFile) {
                        std::cout << "Hot reloading font: " << active->name << " - " << changedFile << "\n";
                        reloadFontSlot(active, slot);
                        break;
                    }
                }
                break;
            }

            case WatchEventType::PRESET_ADDED: {
                std::cout << "New preset directory: " << ev.path << "\n";
                addPreset(ev.path, needsSwap);
                break;
            }

            case WatchEventType::PRESET_REMOVED: {
                std::cout << "Preset directory removed: " << ev.path << "\n";
                if (ev.path == active->shaderDir) {
                    removeActiveFromPresets();
                    needsSwap = true;
                } else {
                    removePresetByPath(ev.path, needsSwap);
                }
                break;
            }
            }
        }
    }

    void removeActiveFromPresets() {
        std::string removedName = active->name;
        if (presets.size() <= 1) {
            active->hasError = true;
            const std::string err = removedName +
                        " - shader directory was deleted. No other presets available.";
            active->errorMessage = formatErrorMessageForPreset(err, active->errorLen);
            std::cerr << err << "\n";
            errorSwap();
            return;
        }
        active->destroyTextures();
        active->destroyFonts();
        presets.erase(presets.begin() + index);
        if (index >= (int)presets.size()) index = (int)presets.size() - 1;
        active = &presets[index];
        const std::string msg = removedName + " was removed. Moving to: " + active->name;
        std::cerr << msg << "\n";
        updateWatcher();
    }

    void errorSwap() {
        globals.showError  = active->hasError;
        globals.errorLen   = active->errorLen;
        globals.errorChars = active->errorMessage;
    }

private:
    std::vector<ShaderPreset> presets;
    Shader                    error;
    Shader                    deviceMenu;
    ShaderWatcher             watcher;
    std::vector<WatchEvent>   pendingEvents;
    std::string               shadersPath;
    int                       index = 0;
    Globals&                  globals;

    void updateWatcher() {
        watcher.updateActivePreset(active->shaderDir,
                                   getTextureFilenames(active),
                                   getFontFilenames(active));
    }

    std::vector<std::string> getTextureFilenames(ShaderPreset* p) {
        std::vector<std::string> names;
        for (auto& t : p->textures) names.push_back(t.filename);
        return names;
    }

    std::vector<std::string> getFontFilenames(ShaderPreset* p) {
        std::vector<std::string> names;
        for (auto& f : p->fonts) names.push_back(f.filename);
        return names;
    }

    // Load a single new preset from a directory path and insert it in
    // sorted order to keep the preset list alphabetically consistent
    void addPreset(const std::string& dirPath, bool& needsSwap) {
        std::string name = std::filesystem::path(dirPath).filename().string();

        // Check for duplicate
        for (auto& p : presets) {
            if (p.name == name) return;
        }

        ShaderPreset p;
        p.name      = name;
        p.shaderDir = dirPath;

        compilePreset(p, "addPreset: ");

        auto insertPos = std::lower_bound(presets.begin(), presets.end(), p,
            [](const ShaderPreset& a, const ShaderPreset& b) {
                return a.name < b.name;
            });
        int insertIdx = (int)std::distance(presets.begin(), insertPos);
        presets.insert(insertPos, std::move(p));

        if (insertIdx <= index) {
            index++;
            active = &presets[index];
            needsSwap = true;
        }
    }

    void removePresetByPath(const std::string& dirPath, bool& needsSwap) {
        for (int i = 0; i < (int)presets.size(); i++) {
            if (presets[i].shaderDir == dirPath) {
                presets[i].destroyTextures();
                presets[i].destroyFonts();
                presets.erase(presets.begin() + i);
                // If the erased preset was before the active one,
                // shift index down to keep pointing at the same preset
                if (i < index) {
                    index--;
                }
                // Pointer may have been invalidated by the erase
                active = &presets[index];
                needsSwap = true;
                return;
            }
        }
    }
};

