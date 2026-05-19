#pragma once

#include "gpu/shader_preset.hpp"
#include "gpu/texture_loader.hpp"
#include "gpu/font_loader.hpp"
#include "config/spec_parser.hpp"
#include "config/globals.hpp"
#include <algorithm>

inline std::string loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "loadFile: could not open " << path << "\n";
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::array<int, 512> formatErrorMessageForPreset(const std::string& msg,
                                                        int& errorLen) {
    errorLen = std::min((int)msg.size(), 512);
    std::array<int, 512> errorChars;
    for (int i = 0; i < errorLen; i++) {
        errorChars[i] = (int)msg[i];
    }
    return errorChars;
}

inline void setPresetError(ShaderPreset& p, const std::string& err) {
    std::cerr << err << "\n";
    p.errorMessage = formatErrorMessageForPreset(err, p.errorLen);
    p.hasError = true;
}
 
inline void updateWriteTimes(ShaderPreset& p) {
    auto fragPath = std::filesystem::path(p.shaderDir) / "frag.glsl";
    auto specPath = std::filesystem::path(p.shaderDir) / "spec.cfg";
    p.lastFragWrite = std::filesystem::exists(fragPath)
                    ? std::filesystem::last_write_time(fragPath)
                    : std::filesystem::file_time_type{};
    p.lastSpecWrite = std::filesystem::exists(specPath)
                    ? std::filesystem::last_write_time(specPath)
                    : std::filesystem::file_time_type{};
}
 
inline void warnSpecScaling(const std::string& name, Spec& spec) {
    const int fftScale = spec.customFFTSizeScalesWithWindow;
    if (spec.fftUsesExprVar[WINDOW_WIDTH] &&
       (fftScale == WIDTH_SCALE || fftScale == RESOLUTION_SCALE)) {
        std::cout << "WARNING: " << name
                  << " FFT custom size doubly scaled by WINDOW_WIDTH "
                  << "due to scale mode and width expression variable usage\n";
    }
    if (spec.fftUsesExprVar[WINDOW_HEIGHT] &&
       (fftScale == HEIGHT_SCALE || fftScale == RESOLUTION_SCALE)) {
        std::cout << "WARNING: " << name
                  << " FFT custom size doubly scaled by WINDOW_HEIGHT "
                  << "due to scale mode and height expression variable usage\n";
    }
    if (spec.customFFTSizeScalesWithWindow != NO_SCALE &&
        spec.fftOutputMode != CUSTOM_SIZE) {
        std::cout << "WARNING: " << name
                  << " FFT is not using custom size mode, but has window scaling. "
                  << "Window scaling set to none.\n";
        spec.customFFTSizeScalesWithWindow = NO_SCALE;
    }
    const int fbScale = spec.feedbackBufferScalesWithWindow;
    if (spec.feedbackUsesExprVar[WINDOW_WIDTH] &&
       (fbScale == WIDTH_SCALE || fbScale == RESOLUTION_SCALE)) {
        std::cout << "WARNING: " << name
                  << " Feedback buffer doubly scaled by WINDOW_WIDTH due to "
                  << "scale mode and width expression variable usage\n";
    }
    if (spec.feedbackUsesExprVar[WINDOW_HEIGHT] &&
       (fbScale == HEIGHT_SCALE || fbScale == RESOLUTION_SCALE)) {
        std::cout << "WARNING: " << name
                  << " Feedback buffer doubly scaled by WINDOW_HEIGHT due to "
                  << "scale mode and height expression variable usage\n";
    }
}
 
// Attempts to parse spec, compile shader, and build textures/fonts for a preset.
// Assumes p.name and p.shaderDir are already set.
// Returns true if the preset loaded successfully (even if with warnings).
// Returns false and sets error state on the preset if anything fails.
inline bool compilePreset(ShaderPreset& p, const std::string parentPrepend) {
    auto fragPath = std::filesystem::path(p.shaderDir) / "frag.glsl";
    auto specPath = std::filesystem::path(p.shaderDir) / "spec.cfg";
    p.spec = Spec{};
    p.hasError = false;
    p.errorMessage = {0};
 
    // Check frag.glsl exists
    if (!std::filesystem::exists(fragPath)) {
        setPresetError(p, parentPrepend + p.name + " - no frag.glsl found");
        return false;
    }
 
    // Load frag source
    std::string fragSrc = loadFile(fragPath.string());
    if (fragSrc.empty()) {
        setPresetError(p, parentPrepend + p.name + " - frag.glsl could not be opened");
        return false;
    }
 
    // Parse spec if present
    if (std::filesystem::exists(specPath)) {
        std::string err = parseSpec(specPath.string(), p.spec);
        if (!err.empty()) {
            setPresetError(p, parentPrepend + p.name + " spec.cfg - " + err);
            updateWriteTimes(p);
            return false;
        }
    }
 
    warnSpecScaling(p.name, p.spec);
 
    // Compile shader
    std::string vtxSrc = getVertexSrc();
    p.shader = Shader();
    std::string err = p.shader.init(vtxSrc.c_str(), fragSrc.c_str());
    updateWriteTimes(p);
 
    if (!err.empty()) {
        setPresetError(p, parentPrepend + p.name + " - shader compile failed - " + err);
        return false;
    }
 
    // Build resources
    buildTextures(p);
    buildFonts(p);
    return true;
}

inline std::vector<ShaderPreset> loadPresets(const std::string& shadersDir) {
    std::vector<ShaderPreset> presets;
 
    if (!std::filesystem::exists(shadersDir)) {
        std::cerr << "loadPresets: directory not found: " << shadersDir << "\n";
        return presets;
    }
 
    std::vector<std::filesystem::directory_entry> entries;
    for (auto& entry : std::filesystem::directory_iterator(shadersDir)) {
        if (entry.is_directory()) {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end());
 
    for (auto& entry : entries) {
        if (!entry.is_directory()) continue;
 
        ShaderPreset p;
        p.name      = entry.path().filename().string();
        p.shaderDir = entry.path().string();
 
        if (compilePreset(p, "loadPreset: ")) {
            std::cout << "loadPresets: loaded " << p.name << "\n";
        } else {
            std::cout << "loadPresets: using ErrorShader in " << p.name << "\n";
        }
        presets.push_back(std::move(p));
    }
    return presets;
}
 
inline void reloadPreset(ShaderPreset* p) {
    p->destroyTextures();
    p->destroyFonts();
 
    if (compilePreset(*p, "reloadPreset: ")) {
        std::cout << "Hot Reload: loaded " << p->name << "\n";
    } else {
        std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
    }
}

inline void assertUserDefinedBufferSizes(ShaderPreset* p, size_t maxFBSize) {
    std::string ret = "";
    //set limits on buffer sizes and warn about double dependencies here
    if (p->spec.fftOutputMode == CUSTOM_SIZE && (p->spec.customFFTSize > 8192)) {
        ret = p->name + " - customFFTSize outside of bounds: 0 to 8192 (inclusive). " +
              "customFFTSize has been set to 0.\n";
        setPresetError(*p, ret);
        p->spec.customFFTSize = 0;
    }
    if (p->spec.feedbackBufferSize > maxFBSize) {
        ret = p->name + " - feedback buffer size (" +
            std::to_string(p->spec.feedbackBufferSize) + " floats) exceeds gpu limit ("
            + std::to_string(maxFBSize) + ". feedbackBufferSize has been set to 0.\n";
        setPresetError(*p, ret);
        p->spec.feedbackBufferSize = 0;
    }
}

inline std::string evalSpecExprs(Spec& spec, ExprContext& ctx) {
    std::string ret = evalExpr(spec.customFFTSizeExpr, ctx,
                               spec.customFFTSize, spec.fftUsesExprVar);
    if (!ret.empty()) return ret;
    ctx.fftArrSize = spec.customFFTSize;
    ctx.isFeedbackExpr = true;
    ret = evalExpr(spec.feedbackBufferSizeExpr, ctx,
                   spec.feedbackBufferSize, spec.feedbackUsesExprVar);
    return ret;
}

//if sampleRate > displayHz * hopSize, lower hopAmount until it hits 1,
//if sr > output still, raise fft order by 1 until output > sr
//cout new values, and change spec values to new values
inline void validateFFTRates(Globals& g, ShaderPreset* s) {
    bool valuesChanged = false;
    FFTOrder specOrder = s->spec.fftOrder;
    HopAmount specHops = s->spec.hopAmount;
    while (g.sampleRate > g.displayHz * g.hopSize) {
        if (s->spec.allowDroppedSamples) {
            valuesChanged = true;
            break;
        }
        if (s->spec.hopAmount > 1) {
            s->spec.hopAmount = static_cast<HopAmount>(s->spec.hopAmount / 2);
            g.hopSize = g.fftSize / s->spec.hopAmount;
            valuesChanged = true;
            continue;
        }
        if (s->spec.fftOrder == 13) {
            const std::string err = "Display Hz: " + std::to_string(g.displayHz) +
                                    ". Sample rate: " + std::to_string(g.sampleRate) +
                                    ". All shaders will not work without a lower sample rate or higher display rate.\n";
            setPresetError(*s, err);
            return;
        }
        s->spec.fftOrder = static_cast<FFTOrder>(s->spec.fftOrder + 1);
        g.hopSize = 1 << s->spec.fftOrder;
        g.fftSize = g.hopSize;
        valuesChanged = true;
    }
    if (valuesChanged) {
        if (s->spec.allowDroppedSamples) {
            std::cout << "fftOrder: " << specOrder << ", hopAmount: " << specHops
                      << ", and displayHz: " << g.displayHz <<
                      " cannot keep up with sample rate: " << g.sampleRate <<
                      ". allowDroppedSamples is enabled, samples will be dropped.\n";
        }
        else {
            std::cout << "fftOrder: " << specOrder << ", hopAmount: " << specHops
                      << ", and displayHz: " << g.displayHz <<
                      " cannot keep up with sample rate: " << g.sampleRate <<
                      ". To avoid buffer overlap, hopAmount is now " << s->spec.hopAmount
                      << " and fftOrder is now " << s->spec.fftOrder << ".\n";
        }
    }
}

//Selective hot-reload helpers

inline void reloadTextureSlot(ShaderPreset* p, TextureSlot& slot) {
    auto fullPath = std::filesystem::path(p->shaderDir) / slot.filename;
    if (!isTextureFilenameSafe(slot.filename)) {
        std::cerr << "reloadTextureSlot: unsafe filename " << slot.filename << "\n";
        return;
    }
    int newW = 0, newH = 0;
    GLuint newId = uploadTexture(fullPath.string(), newW, newH);
    if (!newId) {
        std::cerr << "reloadTextureSlot: upload failed for " << slot.filename << "\n";
        return;
    }
    if (slot.texId) glDeleteTextures(1, &slot.texId);
    slot.texId = newId;
    slot.w = newW;
    slot.h = newH;
    // Re-bind the sampler uniform in case the program is already in use
    p->shader.use();
    auto it = p->shader.samplerLocations.find(slot.uniformName);
    if (it != p->shader.samplerLocations.end() && it->second != -1)
        glUniform1i(it->second, slot.unit);
    std::cout << "reloadTextureSlot: reloaded " << slot.filename << "\n";
}

inline void reloadFontSlot(ShaderPreset* p, FontSlot& slot) {
    auto fullPath = std::filesystem::path(p->shaderDir) / slot.filename;
    if (!isFontFilenameSafe(slot.filename)) {
        std::cerr << "reloadFontSlot: unsafe filename " << slot.filename << "\n";
        return;
    }
    auto fontData = loadFontBytes(fullPath.string());
    if (fontData.empty()) {
        std::cerr << "reloadFontSlot: could not load " << slot.filename << "\n";
        return;
    }
    SdfBakeResult bake;
    if (!bakeSdfAtlas(fontData, FONT_SDF_SIZE, FONT_SDF_PADDING,
                      FONT_FIRST_CHAR, FONT_NUM_GLYPHS, bake)) {
        std::cerr << "reloadFontSlot: bake failed for " << slot.filename << "\n";
        return;
    }
    GLuint newAtlas   = uploadSdfAtlas(bake);
    GLuint newMetrics = uploadMetricsTexture(bake);
    if (!newAtlas || !newMetrics) {
        if (newAtlas)   glDeleteTextures(1, &newAtlas);
        if (newMetrics) glDeleteTextures(1, &newMetrics);
        std::cerr << "reloadFontSlot: GPU upload failed for " << slot.filename << "\n";
        return;
    }
    if (slot.atlasTexId)   glDeleteTextures(1, &slot.atlasTexId);
    if (slot.metricsTexId) glDeleteTextures(1, &slot.metricsTexId);
    slot.atlasTexId   = newAtlas;
    slot.metricsTexId = newMetrics;
    // Re-bind sampler uniforms
    p->shader.use();
    auto itA = p->shader.samplerLocations.find(slot.uniformName);
    if (itA != p->shader.samplerLocations.end() && itA->second != -1)
        glUniform1i(itA->second, slot.atlasUnit);
    auto itM = p->shader.samplerLocations.find(slot.uniformName + "Metrics");
    if (itM != p->shader.samplerLocations.end() && itM->second != -1)
        glUniform1i(itM->second, slot.metricsUnit);
    std::cout << "reloadFontSlot: reloaded " << slot.filename << "\n";
}

