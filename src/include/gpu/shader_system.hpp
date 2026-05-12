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
                        const std::string err = active->name +
                            " error - frag.glsl could not be found.";
                        active->errorMessage = formatErrorMessageForPreset(
                            err, active->errorLen);
                        std::cerr << err << "\n";
                    } else {
                        removeActiveFromPresets();
                    }
                    errorSwap();
                    break;
                }
                std::cout << "Hot Reloading: " << active->name << "\n";
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
                        std::cout << "Hot reloading texture: " << changedFile << "\n";
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
                        std::cout << "Hot reloading font: " << changedFile << "\n";
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
                } else {
                    removePresetByPath(ev.path);
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
        auto fragPath = std::filesystem::path(dirPath) / "frag.glsl";
        if (!std::filesystem::exists(fragPath)) {
            // Directory exists but no frag.glsl yet — editor may still be
            // creating files. The next SHADER_CHANGED event will catch it.
            return;
        }

        // Build a minimal single-preset vector via loadPresets on just this dir
        // by re-using existing loading logic
        std::string name = std::filesystem::path(dirPath).filename().string();

        // Check for duplicate
        for (auto& p : presets) {
            if (p.name == name) return;
        }

        // Use the existing loadPresets machinery on a temporary single-entry
        // by loading from the parent and filtering — simpler: just call the
        // internal loading path directly
        ShaderPreset p;
        p.name      = name;
        p.shaderDir = dirPath;
        p.spec      = Spec{};

        auto specPath = std::filesystem::path(dirPath) / "spec.cfg";
        if (std::filesystem::exists(specPath)) {
            std::string err = parseSpec(specPath.string(), p.spec);
            if (!err.empty()) {
                p.errorMessage = formatErrorMessageForPreset(err, p.errorLen);
                p.hasError = true;
            }
        }

        if (!p.hasError) {
            std::string fragSrc = loadFile(fragPath.string());
            std::string vtxSrc  = getVertexSrc();
            std::string err     = p.shader.init(vtxSrc.c_str(), fragSrc.c_str());
            p.lastFragWrite = std::filesystem::last_write_time(fragPath);
            p.lastSpecWrite = std::filesystem::exists(specPath)
                            ? std::filesystem::last_write_time(specPath)
                            : std::filesystem::file_time_type{};
            if (!err.empty()) {
                p.errorMessage = formatErrorMessageForPreset(err, p.errorLen);
                p.hasError = true;
            } else {
                buildTextures(p);
                buildFonts(p);
            }
        }

        // Insert in sorted position
        auto insertPos = std::lower_bound(presets.begin(), presets.end(), p,
            [](const ShaderPreset& a, const ShaderPreset& b) {
                return a.name < b.name;
            });
        int insertIdx = (int)std::distance(presets.begin(), insertPos);
        presets.insert(insertPos, std::move(p));

        // Adjust active index if the insertion shifted it
        if (insertIdx <= index) index++;
        active = &presets[index];

        std::cout << "Added preset: " << name << "\n";
        needsSwap = false; // don't auto-switch to the new preset
    }

    void removePresetByPath(const std::string& dirPath) {
        for (int i = 0; i < (int)presets.size(); i++) {
            if (presets[i].shaderDir == dirPath) {
                presets[i].destroyTextures();
                presets[i].destroyFonts();
                presets.erase(presets.begin() + i);
                if (index >= (int)presets.size()) index = (int)presets.size() - 1;
                active = &presets[index];
                std::cout << "Removed preset at: " << dirPath << "\n";
                return;
            }
        }
    }
};

