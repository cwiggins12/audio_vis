#pragma once

inline const char* fragmentHeader = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;
uniform float W;
uniform float H;
uniform int numBins;
uniform int numChannels;
uniform int frameCount;
uniform int sampleRate;
uniform int errorChars[128];
uniform int errorLen;
uniform int showError;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
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

// cp437 font
const uint font[760] = uint[760](
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
    vec2 charUV = local / vec2(size);
    if (charCode < 32 || charCode > 126) return 0.0;
    int idx = (charCode - 32) * 8;
    int row = int(charUV.y * 8.0);
    int col = int(charUV.x * 8.0);
    uint rowBits = font[idx + row];
    return float((rowBits >> col) & 1u);
}

float renderCharRotated90(int charCode, vec2 origin, float size, vec2 fragPx) {
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
    uint rowBits = font[idx + row];
    return float((rowBits >> col) & 1u);
}

float renderText(int[128] chars, int len, vec2 origin, float size, vec2 fragPx) {
    float result = 0.0;
    for (int i = 0; i < len; i++) {
        result = max(result, renderCharRotated90(chars[i],
                     origin + vec2(float(i) * size, 0.0),
                     size, fragPx));
    }
    return result;
}

vec2 toPx()     { return vec2(uv.x * W, uv.y * H); }
vec2 toCenter() { return vec2((uv.x - 0.5) * W, (uv.y - 0.5) * H); }
)";

