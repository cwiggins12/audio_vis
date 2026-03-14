#pragma once

#include <glad/gles2.h>
#include <iostream>

class Shader {
public:
    GLuint id;

    Shader(const char* vertSrc, const char* fragSrc) {
        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);

        GLint success;
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(id, 512, nullptr, log);
            std::cerr << "Shader link error:\n" << log << std::endl;
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() {
        glUseProgram(id);
    }

private:
    GLuint compile(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "Shader compile error:\n" << log << std::endl;
        }

        return shader;
    }
};

inline const char* vertexSrc = R"(#version 310 es
precision highp float;

out vec2 uv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexID];
    uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";
/*
inline const char* fragmentSrc = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;
uniform int numBins;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
};

layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};

void main() {
    //map uv.x to a bin index
    int binIndex = int(uv.x * float(numBins));
    binIndex = clamp(binIndex, 0, numBins - 1);

    float db = fftData[binIndex];

    //normalize -96..0 to 0..1
    float normalized =(db + 96.0) / 96.0;
    normalized = clamp(normalized, 0.0, 1.0);

    //draw bar - 1.0 if uv.y is below the bar height, 0.0 otherwise
    float bar = step(uv.y, normalized);

    //color: gradient from blue at bottom to cyan at top
    vec3 color = mix(vec3(0.0, 0.2, 0.8), vec3(0.0, 1.0, 0.9), uv.y) * bar;

    //dark background where there's no bar
    color += vec3(0.05, 0.07, 0.12) * (1.0 - bar);

    FragColor = vec4(color, 1.0);
}
)";
*/

inline const char* fragmentSrc = R"(
#version 310 es
precision highp float;
in vec2 uv;
out vec4 FragColor;
uniform float time;
uniform int numBins;
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

// --- Constants ---
const float W = 1280.0;
const float H = 720.0;

// X layout
const float SP  = 40.0;   // full space
const float HSP = 20.0;   // half space
const float FFT_W = 1000.0;
const float MTR_W = 70.0; // total meter width (35 peak + 35 rms)
const float HALF_MTR = 35.0;

// Y layout
const float MEAS_H  = 640.0;
const float HOLD_H  = 4.0;

// dB range
const float DB_MIN = -96.0;
const float DB_MAX =  0.0;

// Outline thickness in pixels
const float OUTLINE = 2.0;

// Colors
const vec4 COL_FFT   = vec4(0.0, 1.0, 0.0, 1.0);
const vec4 COL_PEAK  = vec4(1.0, 1.0, 0.0, 1.0);
const vec4 COL_RMS   = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 COL_HOLD  = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_OUT   = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_BG    = vec4(0.0, 0.0, 0.0, 1.0);

// X region starts (pixel coords)
// [0      ] space
// [40     ] FFT
// [1040   ] space
// [1080   ] L peak meter
// [1115   ] L rms meter
// [1150   ] half-space
// [1170   ] R peak meter
// [1205   ] R rms meter
// [1240   ] space
// [1280   ] end

const float FFT_X0   = SP;                              // 40
const float FFT_X1   = SP + FFT_W;                     // 1040
const float LPEAK_X0 = FFT_X1 + SP;                    // 1080
const float LPEAK_X1 = LPEAK_X0 + HALF_MTR;            // 1115
const float LRMS_X0  = LPEAK_X1;                       // 1115
const float LRMS_X1  = LRMS_X0 + HALF_MTR;            // 1150
const float RPEAK_X0 = LRMS_X1 + HSP;                  // 1170
const float RPEAK_X1 = RPEAK_X0 + HALF_MTR;            // 1205
const float RRMS_X0  = RPEAK_X1;                       // 1205
const float RRMS_X1  = RRMS_X0 + HALF_MTR;            // 1240

// Y region (pixel coords, 0 = bottom)
const float MEAS_Y0  = SP;               // 40
const float MEAS_Y1  = SP + MEAS_H;     // 680

float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}

// Returns true if px is within [lo, hi) on one axis
bool inRange(float px, float lo, float hi) {
    return px >= lo && px < hi;
}

// Check outline: returns true if pixel is within `thick` px of the rect edge
bool isOutline(float px, float py, float x0, float x1, float y0, float y1, float thick) {
    bool insideX = inRange(px, x0, x1);
    bool insideY = inRange(py, y0, y1);
    if (!insideX || !insideY) return false;
    return px < x0 + thick || px >= x1 - thick ||
           py < y0 + thick || py >= y1 - thick;
}

void main() {
    // Pixel coordinates, Y=0 at bottom
    float px = uv.x * W;
    float py = uv.y * H;

    vec4 color = COL_BG;

    // ------------------------------------------------------------------ FFT
    if (inRange(px, FFT_X0, FFT_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - FFT_X0;
        float innerW = FFT_W - 2.0 * OUTLINE;
        float innerH = MEAS_H - 2.0 * OUTLINE;
        float innerX = localX - OUTLINE;
        float localY = py - MEAS_Y0;
        float innerY = localY - OUTLINE;

        // Outline check
        if (localX < OUTLINE || localX >= FFT_W - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            // Which bin does this x pixel map to?
            int bin = int(innerX / innerW * float(numBins));
            bin = clamp(bin, 0, numBins - 1);

            float fftDb   = fftData[bin];
            float holdDb  = fftHolds[bin];
            float fftT    = dbToT(fftDb)  * innerH;
            float holdT   = dbToT(holdDb) * innerH;

            // Hold bar (4 px tall, drawn on top)
            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fftT) {
                color = COL_FFT;
            }
        }
    }

    // -------------------------------------------------------- Helper macro
    // We'll write a function for a vertical bar meter to avoid repetition
    // Params: buffer index for peak/rms or direct value, holds index
    // We handle each of the 4 meters (Lpeak, Lrms, Rpeak, Rrms) inline.

    // ---- L Peak meter ----
    if (inRange(px, LPEAK_X0, LPEAK_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - LPEAK_X0;
        float localY = py - MEAS_Y0;
        float innerH = MEAS_H - 2.0 * OUTLINE;

        if (localX < OUTLINE || localX >= HALF_MTR - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            float innerY  = localY - OUTLINE;
            float fillT   = dbToT(peakRmsData[0]) * innerH;
            float holdT   = dbToT(prHolds[0])     * innerH;
            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fillT) {
                color = COL_PEAK;
            }
        }
    }

    // ---- L RMS meter ----
    if (inRange(px, LRMS_X0, LRMS_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - LRMS_X0;
        float localY = py - MEAS_Y0;
        float innerH = MEAS_H - 2.0 * OUTLINE;

        if (localX < OUTLINE || localX >= HALF_MTR - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            float innerY  = localY - OUTLINE;
            float fillT   = dbToT(peakRmsData[1]) * innerH;
            float holdT   = dbToT(prHolds[1])     * innerH;
            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fillT) {
                color = COL_RMS;
            }
        }
    }

    // ---- R Peak meter ----
    if (inRange(px, RPEAK_X0, RPEAK_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - RPEAK_X0;
        float localY = py - MEAS_Y0;
        float innerH = MEAS_H - 2.0 * OUTLINE;

        if (localX < OUTLINE || localX >= HALF_MTR - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            float innerY  = localY - OUTLINE;
            float fillT   = dbToT(peakRmsData[2]) * innerH;
            float holdT   = dbToT(prHolds[2])     * innerH;
            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fillT) {
                color = COL_PEAK;
            }
        }
    }

    // ---- R RMS meter ----
    if (inRange(px, RRMS_X0, RRMS_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - RRMS_X0;
        float localY = py - MEAS_Y0;
        float innerH = MEAS_H - 2.0 * OUTLINE;

        if (localX < OUTLINE || localX >= HALF_MTR - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            float innerY  = localY - OUTLINE;
            float fillT   = dbToT(peakRmsData[3]) * innerH;
            float holdT   = dbToT(prHolds[3])     * innerH;
            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fillT) {
                color = COL_RMS;
            }
        }
    }

    FragColor = color;
}
)";
//glsl logic I need: 
//guaranteed 2 channels for this test shader
//x = space->fft->space->l-meter->half-space->r-meter->space
//fft = 1000, 2x meter = 70(35 peak, 35 rms), space = 40, half-space = 20
//total = 1280 = 40 + 1000 + 40 + 70 + 20 + 70 + 40
//
//y = space->measurements->space
//measurements = 640, space = 40
//total = 720 = 40 + 640 + 40
//
//peak and rms meters fill based off of a single scalar each 
//order is peakl, rmsl, peakr, rmsr in buffer and in window
//order is the same for the holds with should be 4px y and the same width
//
//for fft, each pixel will be a bin as given by the buffer in order
//holds will be in the same order in the holds buffer and the same dims as the other holds
//
//all values will be given in dB with a range of -96.0 - 0.0
//
//I want the fft to be green, peak to be yellow, rms to be red, and all holds to be white
//white holds should be on top of the other measurements
