#pragma once

#include "utils/gl_platform.hpp"
#include <string>

inline std::string getFragmentHeader() {
    std::string header = glslVersionString();
#ifdef USE_GLES
    header += "precision highp float;\n";
#endif
    header += R"(
in vec2 v_pos;
out vec4 FragColor;

layout(std140, binding = 0) uniform FrameUniforms {
    float mouseX;
    float mouseY;
    float time;
    float W;
    float H;
    int mouseDown;
    int fftOrder;
    int fftSize;
    int hopAmt;
    int hopSize;
    int fftBinAmt;
    int fftArrSize;
    int newAudioWindow;
    int numChannels;
    int displayHz;
    int sampleRate;
    int showError;
    int errorLen;
    int showDeviceMenu;
    int deviceMenuLen;
    ivec4 errorChars[128];
    ivec4 deviceChars[128];
};
layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRMSData[];
};
layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};
layout(std430, binding = 2) readonly buffer PRHolds {
    float prHolds[];
};
layout(std430, binding = 3) readonly buffer FFTHolds {
    float fftHolds[];
};
layout(std430, binding = 4) readonly buffer FeedbackRead {
    float feedbackIn[];
};
layout(std430, binding = 5) writeonly buffer FeedbackWrite {
    float feedbackOut[];
};
layout(std430, binding = 6) readonly buffer RawSamples {
    float rawSamples[];
};

// cp437 font
const uint cp437[760] = uint[760](
  0u,   0u,   0u,   0u,   0u,   0u,   0u,   0u, //' '
  0u,   6u,  95u,  95u,   6u,   0u,   0u,   0u, //'!'
  0u,   7u,   7u,   0u,   7u,   7u,   0u,   0u, //'"'
 20u, 127u, 127u,  20u, 127u, 127u,  20u,   0u, //'#'
 36u,  46u, 107u, 107u,  58u,  18u,   0u,   0u, //'$'
 70u, 102u,  48u,  24u,  12u, 102u,  98u,   0u, //'%'
 48u, 122u,  79u,  93u,  55u, 122u,  72u,   0u, //'&'
  4u,   7u,   3u,   0u,   0u,   0u,   0u,   0u, //'''
  0u,  28u,  62u,  99u,  65u,   0u,   0u,   0u, //'('
  0u,  65u,  99u,  62u,  28u,   0u,   0u,   0u, //')'
  8u,  42u,  62u,  28u,  28u,  62u,  42u,   8u, //'*'
  8u,   8u,  62u,  62u,   8u,   8u,   0u,   0u, //'+'
  0u, 128u, 224u,  96u,   0u,   0u,   0u,   0u, //','
  8u,   8u,   8u,   8u,   8u,   8u,   0u,   0u, //'-'
  0u,   0u,  96u,  96u,   0u,   0u,   0u,   0u, //'.'
 96u,  48u,  24u,  12u,   6u,   3u,   1u,   0u, //'/'
 62u, 127u, 113u,  89u,  77u, 127u,  62u,   0u, //'0'
 64u,  66u, 127u, 127u,  64u,  64u,   0u,   0u, //'1'
 98u, 115u,  89u,  73u, 111u, 102u,   0u,   0u, //'2'
 34u,  99u,  73u,  73u, 127u,  54u,   0u,   0u, //'3'
 24u,  28u,  22u,  83u, 127u, 127u,  80u,   0u, //'4'
 39u, 103u,  69u,  69u, 125u,  57u,   0u,   0u, //'5'
 60u, 126u,  75u,  73u, 121u,  48u,   0u,   0u, //'6'
  3u,   3u, 113u, 121u,  15u,   7u,   0u,   0u, //'7'
 54u, 127u,  73u,  73u, 127u,  54u,   0u,   0u, //'8'
  6u,  79u,  73u, 105u,  63u,  30u,   0u,   0u, //'9'
  0u,   0u, 102u, 102u,   0u,   0u,   0u,   0u, //':'
  0u, 128u, 230u, 102u,   0u,   0u,   0u,   0u, //';'
  8u,  28u,  54u,  99u,  65u,   0u,   0u,   0u, //'<'
 36u,  36u,  36u,  36u,  36u,  36u,   0u,   0u, //'='
  0u,  65u,  99u,  54u,  28u,   8u,   0u,   0u, //'>'
  2u,   3u,  81u,  89u,  15u,   6u,   0u,   0u, //'?'
 62u, 127u,  65u,  93u,  93u,  31u,  30u,   0u, //'@'
124u, 126u,  19u,  19u, 126u, 124u,   0u,   0u, //'A'
 65u, 127u, 127u,  73u,  73u, 127u,  54u,   0u, //'B'
 28u,  62u,  99u,  65u,  65u,  99u,  34u,   0u, //'C'
 65u, 127u, 127u,  65u,  99u,  62u,  28u,   0u, //'D'
 65u, 127u, 127u,  73u,  93u,  65u,  99u,   0u, //'E'
 65u, 127u, 127u,  73u,  29u,   1u,   3u,   0u, //'F'
 28u,  62u,  99u,  65u,  81u, 115u, 114u,   0u, //'G'
127u, 127u,   8u,   8u, 127u, 127u,   0u,   0u, //'H'
  0u,  65u, 127u, 127u,  65u,   0u,   0u,   0u, //'I'
 48u, 112u,  64u,  65u, 127u,  63u,   1u,   0u, //'J'
 65u, 127u, 127u,   8u,  28u, 119u,  99u,   0u, //'K'
 65u, 127u, 127u,  65u,  64u,  96u, 112u,   0u, //'L'
127u, 127u,  14u,  28u,  14u, 127u, 127u,   0u, //'M'
127u, 127u,   6u,  12u,  24u, 127u, 127u,   0u, //'N'
 28u,  62u,  99u,  65u,  99u,  62u,  28u,   0u, //'O'
 65u, 127u, 127u,  73u,   9u,  15u,   6u,   0u, //'P'
 30u,  63u,  33u, 113u, 127u,  94u,   0u,   0u, //'Q'
 65u, 127u, 127u,   9u,  25u, 127u, 102u,   0u, //'R'
 38u, 111u,  77u,  89u, 115u,  50u,   0u,   0u, //'S'
  3u,  65u, 127u, 127u,  65u,   3u,   0u,   0u, //'T'
127u, 127u,  64u,  64u, 127u, 127u,   0u,   0u, //'U'
 31u,  63u,  96u,  96u,  63u,  31u,   0u,   0u, //'V'
127u, 127u,  48u,  24u,  48u, 127u, 127u,   0u, //'W'
 67u, 103u,  60u,  24u,  60u, 103u,  67u,   0u, //'X'
  7u,  79u, 120u, 120u,  79u,   7u,   0u,   0u, //'Y'
 71u,  99u, 113u,  89u,  77u, 103u, 115u,   0u, //'Z'
  0u, 127u, 127u,  65u,  65u,   0u,   0u,   0u, //'['
  1u,   3u,   6u,  12u,  24u,  48u,  96u,   0u, //'\'
  0u,  65u,  65u, 127u, 127u,   0u,   0u,   0u, //']'
  8u,  12u,   6u,   3u,   6u,  12u,   8u,   0u, //'^'
128u, 128u, 128u, 128u, 128u, 128u, 128u, 128u, //'_'
  0u,   0u,   3u,   7u,   4u,   0u,   0u,   0u, //'`'
 32u, 116u,  84u,  84u,  60u, 120u,  64u,   0u, //'a'
 65u, 127u,  63u,  72u,  72u, 120u,  48u,   0u, //'b'
 56u, 124u,  68u,  68u, 108u,  40u,   0u,   0u, //'c'
 48u, 120u,  72u,  73u,  63u, 127u,  64u,   0u, //'d'
 56u, 124u,  84u,  84u,  92u,  24u,   0u,   0u, //'e'
 72u, 126u, 127u,  73u,   3u,   2u,   0u,   0u, //'f'
152u, 188u, 164u, 164u, 248u, 124u,   4u,   0u, //'g'
 65u, 127u, 127u,   8u,   4u, 124u, 120u,   0u, //'h'
  0u,  68u, 125u, 125u,  64u,   0u,   0u,   0u, //'i'
 96u, 224u, 128u, 128u, 253u, 125u,   0u,   0u, //'j'
 65u, 127u, 127u,  16u,  56u, 108u,  68u,   0u, //'k'
  0u,  65u, 127u, 127u,  64u,   0u,   0u,   0u, //'l'
124u, 124u,  24u,  56u,  28u, 124u, 120u,   0u, //'m'
124u, 124u,   4u,   4u, 124u, 120u,   0u,   0u, //'n'
 56u, 124u,  68u,  68u, 124u,  56u,   0u,   0u, //'o'
132u, 252u, 248u, 164u,  36u,  60u,  24u,   0u, //'p'
 24u,  60u,  36u, 164u, 248u, 252u, 132u,   0u, //'q'
 68u, 124u, 120u,  76u,   4u,  28u,  24u,   0u, //'r'
 72u,  92u,  84u,  84u, 116u,  36u,   0u,   0u, //'s'
  0u,   4u,  62u, 127u,  68u,  36u,   0u,   0u, //'t'
 60u, 124u,  64u,  64u,  60u, 124u,  64u,   0u, //'u'
 28u,  60u,  96u,  96u,  60u,  28u,   0u,   0u, //'v'
 60u, 124u, 112u,  56u, 112u, 124u,  60u,   0u, //'w'
 68u, 108u,  56u,  16u,  56u, 108u,  68u,   0u, //'x'
156u, 188u, 160u, 160u, 252u, 124u,   0u,   0u, //'y'
 76u, 100u, 116u,  92u,  76u, 100u,   0u,   0u, //'z'
  8u,   8u,  62u, 119u,  65u,  65u,   0u,   0u, //'{'
  0u,   0u,   0u, 119u, 119u,   0u,   0u,   0u, //'|'
 65u,  65u, 119u,  62u,   8u,   8u,   0u,   0u, //'}'
  2u,   3u,   1u,   3u,   2u,   3u,   1u,   0u  //'~'
 );

float renderChar(int charCode, vec2 origin, float size, vec2 fragPx) {
    vec2 local = fragPx - origin;
    if (local.x < 0.0 || local.x >= size ||
        local.y < 0.0 || local.y >= size)
        return 0.0;
    vec2 rotated = vec2(size - 1.0 - local.y, local.x);
    vec2 charUV = rotated / vec2(size);
    if (charCode < 32 || charCode > 126) return 0.0;
    int idx = (charCode - 32) * 8;
    int row = int(charUV.y * 8.0);
    int col = int(charUV.x * 8.0);
    uint rowBits = cp437[idx + row];
    return float((rowBits >> col) & 1u);
}

float renderText(int[128] chars, int len, vec2 origin, float size,
                 vec2 fragPx, int offset) {
    float localX = fragPx.x - origin.x;
    float localY = fragPx.y - origin.y;
    if (localX < 0.0 || localY < 0.0 ||
        localY >= size || localX >= size * float(len)) return 0.0;
    int i = int(localX / size);
    if (i >= len) return 0.0;
    return renderChar(chars[offset + i],
                      origin + vec2(float(i) * size, 0.0),
                      size, fragPx);
}

int getPackedChar(ivec4 arr[128], int i) {
    return arr[i / 4][i % 4];
}

float renderTextPacked(ivec4 chars[128], int len, vec2 origin, float size,
                          vec2 fragPx, int offset) {
    float localX = fragPx.x - origin.x;
    float localY = fragPx.y - origin.y;
    if (localX < 0.0 || localY < 0.0 ||
        localY >= size || localX >= size * float(len)) return 0.0;
    int i = int(localX / size);
    return renderChar(getPackedChar(chars, offset + i),
                      origin + vec2(float(i) * size, 0.0),
                      size, fragPx);
}

//spacing covention helpers
// UV (0,0) = bottom-left, (1,1) = top-right — GL/math convention
vec2 uvBottomLeft() {
    return v_pos * 0.5 + 0.5;
}

// UV (0,0) = top-left, (1,1) = bottom-right — image/screen convention
vec2 uvTopLeft() {
    return vec2(v_pos.x * 0.5 + 0.5, v_pos.y * -0.5 + 0.5);
}

// NDC (-1,-1) = bottom-left, (1,1) = top-right — Y up
vec2 ndcBottomLeft() {
    return v_pos;
}

// NDC (-1,-1) = top-left, (1,1) = bottom-right — Y down
vec2 ndcTopLeft() {
    return vec2(v_pos.x, -v_pos.y);
}

// Aspect ratio corrected — use when drawing circles, SDFs, anything
// that needs to look geometrically correct on non-square viewports
vec2 ndcBottomLeftAR() {
    return vec2(v_pos.x * (W / H), v_pos.y);
}

vec2 ndcTopLeftAR() {
    return vec2(v_pos.x * (W / H), -v_pos.y);
}

// Pixel space helpers
vec2 toPx() {
    vec2 uv = uvBottomLeft();
    return vec2(uv.x * W, uv.y * H);
}

vec2 toCenter() {
    vec2 uv = uvBottomLeft();
    return vec2((uv.x - 0.5) * W, (uv.y - 0.5) * H);
}

// SDF font rendering utilities
//
// Metrics texture layout (per column = one glyph):
//   row 0 : u0  v0  u1  v1           atlas UV rect
//   row 1 : advance  bearingX  bearingY  glyphW    (normalised to fontSize)
//   row 2 : glyphH   0         0         0
//
// All metric values are normalised so that multiplying by 'size' gives pixels.
// firstChar / numGlyphs must match the range baked into the atlas (default 32–126).
 
// Core SDF sample with screen-space anti-aliasing
float sdfSample(sampler2D atlas, vec2 uv) {
    float d = texture(atlas, uv).r;
    float w = fwidth(d);
    return smoothstep(0.5 - w, 0.5 + w, d);
}
 
// Render one SDF glyph.  penPos is baseline-left in pixel space.
// Returns coverage (0..1) for the current fragment.
float renderSdfChar(sampler2D atlas, sampler2D metrics,
                    int charCode, vec2 penPos, float size,
                    vec2 fragPx, int firstChar, int numGlyphs) {
    int idx = charCode - firstChar;
    if (idx < 0 || idx >= numGlyphs) return 0.0;
 
    vec4 uvRect = texelFetch(metrics, ivec2(idx, 0), 0);
    vec4 met1   = texelFetch(metrics, ivec2(idx, 1), 0);
    vec4 met2   = texelFetch(metrics, ivec2(idx, 2), 0);
 
    float bearingX = met1.y;
    float bearingY = met1.z;
    float glyphW   = met1.w;
    float glyphH   = met2.x;
 
    // glyph quad in pixel space (origin = bottom-left of quad)
    float x0 = penPos.x + bearingX * size;
    float y0 = penPos.y + (bearingY - glyphH) * size;
    float w  = glyphW * size;
    float h  = glyphH * size;
 
    vec2 local = fragPx - vec2(x0, y0);
    if (local.x < 0.0 || local.x >= w ||
        local.y < 0.0 || local.y >= h)
        return 0.0;
 
    vec2 t  = local / vec2(w, h);
    vec2 uv = vec2(mix(uvRect.x, uvRect.z, t.x),
                   mix(uvRect.y, uvRect.w, t.y));
 
    return sdfSample(atlas, uv);
}
 
// Horizontal advance for a character in pixels at the given size
float sdfAdvance(sampler2D metrics, int charCode, float size,
                 int firstChar, int numGlyphs) {
    int idx = charCode - firstChar;
    if (idx < 0 || idx >= numGlyphs) return 0.0;
    return texelFetch(metrics, ivec2(idx, 1), 0).x * size;
}

// Render an int array text string using SDF font.
// origin.y is the text baseline.
float renderSdfText(sampler2D atlas, sampler2D metrics,
                    int chars[128], int len, vec2 origin, float size,
                    vec2 fragPx, int offset,
                    int firstChar, int numGlyphs) {
    if (fragPx.y < origin.y || fragPx.y > origin.y + size ||
        fragPx.x < origin.x || fragPx.x > origin.x + size * float(len))
        return 0.0;
 
    float result = 0.0;
    float penX   = origin.x;
 
    for (int i = 0; i < len; i++) {
        if (penX - size > fragPx.x) break;
        int cc = chars[offset + i];
        result = max(result,
                     renderSdfChar(atlas, metrics, cc,
                                   vec2(penX, origin.y), size,
                                   fragPx, firstChar, numGlyphs));
        penX += sdfAdvance(metrics, cc, size, firstChar, numGlyphs);
    }
    return result;
}

#line 1
)";
    return header;
}
