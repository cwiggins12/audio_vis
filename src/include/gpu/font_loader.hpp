#pragma once

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION

#include "gpu/shader_preset.hpp"
#include "stb/stb_rect_pack.h"
#include "stb/stb_truetype.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>

//hardcoded for now, structured for future spec.cfg exposure
static const int   FONT_FIRST_CHAR  = 32;
static const int   FONT_LAST_CHAR   = 126;
static const int   FONT_NUM_GLYPHS  = FONT_LAST_CHAR - FONT_FIRST_CHAR + 1;
static const float FONT_SDF_SIZE    = 48.0f;   // px height for SDF rasterisation
static const int   FONT_SDF_PADDING = 6;       // texels of distance-field spread

// stbtt SDF params derived from the above
static const unsigned char FONT_SDF_ONEDGE_VALUE   = 128;
static const float         FONT_SDF_PIXEL_DIST_SCALE =
    128.0f / static_cast<float>(FONT_SDF_PADDING);

inline bool isFontFilenameSafe(const std::string& filename) {
    if (filename.empty()) return false;
    if (filename.find('/')  != std::string::npos) return false;
    if (filename.find('\\') != std::string::npos) return false;
    if (filename.find("..") != std::string::npos) return false;
    return true;
}

// Load raw TTF/OTF bytes
inline std::vector<unsigned char> loadFontBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Intermediate result from the CPU bake step
struct SdfBakeResult {
    std::vector<unsigned char> atlasPixels;
    int atlasW = 0;
    int atlasH = 0;
    int numGlyphs  = 0;
    int firstChar  = 0;
    // Per-glyph metrics laid out for a numGlyphs × 3 RGBA32F texture:
    //   row 0 : u0, v0, u1, v1      (atlas UVs)
    //   row 1 : advance, bearingX, bearingY, glyphW   (normalised to fontSize)
    //   row 2 : glyphH, 0, 0, 0
    std::vector<float> metricsData;   // numGlyphs * 3 * 4 floats
};

// Bake an SDF atlas from a TTF byte buffer
inline bool bakeSdfAtlas(const std::vector<unsigned char>& fontData,
                         float fontSize, int padding,
                         int firstChar, int numGlyphs,
                         SdfBakeResult& result) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fontData.data(),
                        stbtt_GetFontOffsetForIndex(fontData.data(), 0))) {
        std::cerr << "bakeSdfAtlas: stbtt_InitFont failed\n";
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&info, fontSize);

    //collect per-glyph SDF bitmaps
    struct GlyphBmp {
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        int xoff = 0, yoff = 0;
        int advanceWidth = 0, lsb = 0;
    };
    std::vector<GlyphBmp> glyphs(numGlyphs);

    for (int i = 0; i < numGlyphs; i++) {
        int cp  = firstChar + i;
        int gid = stbtt_FindGlyphIndex(&info, cp);
        stbtt_GetGlyphHMetrics(&info, gid, &glyphs[i].advanceWidth, &glyphs[i].lsb);
        glyphs[i].pixels = stbtt_GetGlyphSDF(
            &info, scale, gid,
            padding, FONT_SDF_ONEDGE_VALUE, FONT_SDF_PIXEL_DIST_SCALE,
            &glyphs[i].w, &glyphs[i].h,
            &glyphs[i].xoff, &glyphs[i].yoff);
    }

    //rectangle-pack into an atlas
    int atlasW = 512, atlasH = 512;
    std::vector<stbrp_rect> rects(numGlyphs);
    for (int i = 0; i < numGlyphs; i++) {
        rects[i].id = i;
        rects[i].w  = static_cast<stbrp_coord>(glyphs[i].w > 0 ? glyphs[i].w : 1);
        rects[i].h  = static_cast<stbrp_coord>(glyphs[i].h > 0 ? glyphs[i].h : 1);
    }

    stbrp_context rpCtx;
    std::vector<stbrp_node> rpNodes(atlasW);
    stbrp_init_target(&rpCtx, atlasW, atlasH, rpNodes.data(),
                      static_cast<int>(rpNodes.size()));

    if (!stbrp_pack_rects(&rpCtx, rects.data(), numGlyphs)) {
        atlasW = 1024; atlasH = 1024;
        rpNodes.resize(atlasW);
        stbrp_init_target(&rpCtx, atlasW, atlasH, rpNodes.data(),
                          static_cast<int>(rpNodes.size()));
        if (!stbrp_pack_rects(&rpCtx, rects.data(), numGlyphs)) {
            std::cerr << "bakeSdfAtlas: packing failed at 1024×1024\n";
            for (auto& g : glyphs)
                if (g.pixels) stbtt_FreeSDF(g.pixels, nullptr);
            return false;
        }
    }

    //composite glyphs into atlas buffer
    result.atlasW = atlasW;
    result.atlasH = atlasH;
    result.atlasPixels.resize(atlasW * atlasH, 0);

    for (int i = 0; i < numGlyphs; i++) {
        if (!glyphs[i].pixels || glyphs[i].w <= 0 || glyphs[i].h <= 0) continue;
        int ox = rects[i].x;
        int oy = rects[i].y;
        for (int y = 0; y < glyphs[i].h; y++)
            for (int x = 0; x < glyphs[i].w; x++)
                result.atlasPixels[(oy + y) * atlasW + (ox + x)] =
                    glyphs[i].pixels[y * glyphs[i].w + x];
    }

    //flip atlas vertically so V=0 is bottom (GL convention)
    for (int y = 0; y < atlasH / 2; y++) {
        int y2 = atlasH - 1 - y;
        for (int x = 0; x < atlasW; x++)
            std::swap(result.atlasPixels[y  * atlasW + x],
                      result.atlasPixels[y2 * atlasW + x]);
    }

    //build per-glyph metrics texture data
    //
    // Layout: numGlyphs × 3  RGBA32F
    //   row 0 :  u0   v0   u1   v1     (atlas UVs, post-flip)
    //   row 1 :  adv  bX   bY   gW     (normalised to fontSize)
    //   row 2 :  gH   0    0    0
    //
    float invW = 1.0f / static_cast<float>(atlasW);
    float invH = 1.0f / static_cast<float>(atlasH);
    result.numGlyphs = numGlyphs;
    result.firstChar = firstChar;
    result.metricsData.resize(numGlyphs * 3 * 4, 0.0f);

    for (int i = 0; i < numGlyphs; i++) {
        // pre-flip atlas UVs
        float rawV0 = static_cast<float>(rects[i].y)                   * invH;
        float rawV1 = static_cast<float>(rects[i].y + glyphs[i].h)     * invH;
        // post-flip: newV = 1 - oldV, and swap so v0 < v1
        float v0 = 1.0f - rawV1;
        float v1 = 1.0f - rawV0;

        float u0 = static_cast<float>(rects[i].x)                      * invW;
        float u1 = static_cast<float>(rects[i].x + glyphs[i].w)        * invW;

        float advance  = static_cast<float>(glyphs[i].advanceWidth) * scale / fontSize;
        float bearingX = static_cast<float>(glyphs[i].xoff) / fontSize;
        float bearingY = static_cast<float>(-glyphs[i].yoff) / fontSize; // flip for GL
        float glyphW   = static_cast<float>(glyphs[i].w) / fontSize;
        float glyphH   = static_cast<float>(glyphs[i].h) / fontSize;

        // row 0
        int r0 = i * 4;
        result.metricsData[r0 + 0] = u0;
        result.metricsData[r0 + 1] = v0;
        result.metricsData[r0 + 2] = u1;
        result.metricsData[r0 + 3] = v1;

        // row 1  (offset by numGlyphs * 4)
        int r1 = numGlyphs * 4 + i * 4;
        result.metricsData[r1 + 0] = advance;
        result.metricsData[r1 + 1] = bearingX;
        result.metricsData[r1 + 2] = bearingY;
        result.metricsData[r1 + 3] = glyphW;

        // row 2  (offset by numGlyphs * 8)
        int r2 = numGlyphs * 8 + i * 4;
        result.metricsData[r2 + 0] = glyphH;
    }

    for (auto& g : glyphs)
        if (g.pixels) stbtt_FreeSDF(g.pixels, nullptr);

    std::cout << "bakeSdfAtlas: baked " << numGlyphs << " glyphs into "
              << atlasW << "×" << atlasH << " atlas\n";
    return true;
}

// GPU upload helpers
inline GLuint uploadSdfAtlas(const SdfBakeResult& result) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 result.atlasW, result.atlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, result.atlasPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

inline GLuint uploadMetricsTexture(const SdfBakeResult& result) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
                 result.numGlyphs, 3, 0,
                 GL_RGBA, GL_FLOAT, result.metricsData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// Build / destroy FontSlots for a preset
inline void buildFonts(ShaderPreset* p) {
    p->destroyFonts();

    // font texture units start after the last image-texture unit
    int unit = static_cast<int>(p->textures.size());

    for (auto& [uniformName, filename] : p->spec.fonts) {
        if (!isFontFilenameSafe(filename)) {
            std::cerr << "buildFonts: rejected unsafe font path \""
                      << filename << "\" for " << uniformName << "\n";
            continue;
        }

        auto fullPath = std::filesystem::path(p->shaderDir) / filename;
        auto fontData = loadFontBytes(fullPath.string());
        if (fontData.empty()) {
            std::cerr << "buildFonts: could not load " << fullPath << "\n";
            continue;
        }

        SdfBakeResult bake;
        if (!bakeSdfAtlas(fontData, FONT_SDF_SIZE, FONT_SDF_PADDING,
                          FONT_FIRST_CHAR, FONT_NUM_GLYPHS, bake)) {
            std::cerr << "buildFonts: bake failed for " << uniformName
                      << " -> " << filename << "\n";
            continue;
        }

        GLuint atlasId   = uploadSdfAtlas(bake);
        GLuint metricsId = uploadMetricsTexture(bake);
        if (!atlasId || !metricsId) {
            if (atlasId)   glDeleteTextures(1, &atlasId);
            if (metricsId) glDeleteTextures(1, &metricsId);
            std::cerr << "buildFonts: GPU upload failed for " << uniformName << "\n";
            continue;
        }

        FontSlot slot;
        slot.uniformName = uniformName;
        slot.filename    = filename;
        slot.atlasTexId  = atlasId;
        slot.metricsTexId = metricsId;
        slot.atlasUnit   = unit++;
        slot.metricsUnit = unit++;
        p->fonts.push_back(slot);

        std::cout << "buildFonts: loaded " << uniformName
                  << " -> " << filename
                  << " (atlas unit " << slot.atlasUnit
                  << ", metrics unit " << slot.metricsUnit << ")\n";
    }

    // Resolve sampler locations and bind units
    if (!p->fonts.empty()) {
        std::vector<std::string> names;
        for (auto& f : p->fonts) {
            names.push_back(f.uniformName);
            names.push_back(f.uniformName + "Metrics");
        }
        p->shader.addSamplerLocations(names);

        p->shader.use();
        for (auto& f : p->fonts) {
            auto itA = p->shader.samplerLocations.find(f.uniformName);
            if (itA != p->shader.samplerLocations.end() && itA->second != -1)
                glUniform1i(itA->second, f.atlasUnit);

            auto itM = p->shader.samplerLocations.find(f.uniformName + "Metrics");
            if (itM != p->shader.samplerLocations.end() && itM->second != -1)
                glUniform1i(itM->second, f.metricsUnit);
        }
    }
}

inline void buildFonts(ShaderPreset& p) { buildFonts(&p); }

// Bind all font textures for a preset. Call once per frame before draw.
inline void bindFonts(const ShaderPreset* p) {
    for (auto& f : p->fonts) {
        glActiveTexture(GL_TEXTURE0 + f.atlasUnit);
        glBindTexture(GL_TEXTURE_2D, f.atlasTexId);
        glActiveTexture(GL_TEXTURE0 + f.metricsUnit);
        glBindTexture(GL_TEXTURE_2D, f.metricsTexId);
    }
}

inline void unbindFonts(const ShaderPreset* p) {
    for (auto& f : p->fonts) {
        glActiveTexture(GL_TEXTURE0 + f.atlasUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + f.metricsUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

inline void bindFonts(const ShaderPreset& p)   { bindFonts(&p);   }
inline void unbindFonts(const ShaderPreset& p)  { unbindFonts(&p); }

