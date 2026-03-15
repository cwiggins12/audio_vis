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

inline const char* fragmentSrc = R"(
#version 310 es
precision highp float;
in vec2 uv;
out vec4 FragColor;
uniform float time;
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

const float W = 1280.0;
const float H = 720.0;

const float SP       = 40.0;
const float HSP      = 20.0;
const float FFT_W    = 1000.0;
const float HALF_MTR = 35.0;
const float MEAS_H   = 640.0;
const float HOLD_H   = 4.0;

const float DB_MIN = -96.0;
const float DB_MAX =  0.0;
const float OUTLINE = 2.0;

const vec4 COL_FFT  = vec4(0.0, 1.0, 0.0, 1.0);
const vec4 COL_PEAK = vec4(1.0, 1.0, 0.0, 1.0);
const vec4 COL_RMS  = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 COL_HOLD = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_OUT  = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_BG   = vec4(0.0, 0.0, 0.0, 1.0);

const float FFT_X0   = SP;
const float FFT_X1   = SP + FFT_W;
const float LPEAK_X0 = FFT_X1 + SP;
const float LPEAK_X1 = LPEAK_X0 + HALF_MTR;
const float LRMS_X0  = LPEAK_X1;
const float LRMS_X1  = LRMS_X0 + HALF_MTR;
const float RPEAK_X0 = LRMS_X1 + HSP;
const float RPEAK_X1 = RPEAK_X0 + HALF_MTR;
const float RRMS_X0  = RPEAK_X1;
const float RRMS_X1  = RRMS_X0 + HALF_MTR;

const float MEAS_Y0 = SP;
const float MEAS_Y1 = SP + MEAS_H;

float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}

bool inRange(float px, float lo, float hi) {
    return px >= lo && px < hi;
}

void drawMeter(float localX, float localY, float fillDb, float holdDb,
               float width, vec4 fillCol, inout vec4 color) {
    float innerH = MEAS_H - 2.0 * OUTLINE;
    if (localX < OUTLINE || localX >= width - OUTLINE ||
        localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
        color = COL_OUT;
    } else {
        float innerY = localY - OUTLINE;
        float fillT  = dbToT(fillDb) * innerH;
        float holdT  = dbToT(holdDb) * innerH;
        if (innerY >= holdT - HOLD_H && innerY < holdT) {
            color = COL_HOLD;
        } else if (innerY < fillT) {
            color = fillCol;
        }
    }
}

void main() {
    float px = uv.x * W;
    float py = uv.y * H;

    vec4 color = COL_BG;

    // FFT
    if (inRange(px, FFT_X0, FFT_X1) && inRange(py, MEAS_Y0, MEAS_Y1)) {
        float localX = px - FFT_X0;
        float localY = py - MEAS_Y0;
        float innerH = MEAS_H - 2.0 * OUTLINE;

        if (localX < OUTLINE || localX >= FFT_W - OUTLINE ||
            localY < OUTLINE || localY >= MEAS_H - OUTLINE) {
            color = COL_OUT;
        } else {
            int bin = int(localX - OUTLINE);
            float innerY = localY - OUTLINE;
            float fftDb  = fftData[bin];
            float holdDb = fftHolds[bin];
            float fftT   = dbToT(fftDb)  * innerH;
            float holdT  = dbToT(holdDb) * innerH;

            if (innerY >= holdT - HOLD_H && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fftT) {
                color = COL_FFT;
            }
        }
    }

    // L Peak
    if (inRange(px, LPEAK_X0, LPEAK_X1) && inRange(py, MEAS_Y0, MEAS_Y1))
        drawMeter(px - LPEAK_X0, py - MEAS_Y0, peakRmsData[0], prHolds[0], HALF_MTR, COL_PEAK, color);

    // L RMS
    if (inRange(px, LRMS_X0, LRMS_X1) && inRange(py, MEAS_Y0, MEAS_Y1))
        drawMeter(px - LRMS_X0, py - MEAS_Y0, peakRmsData[1], prHolds[1], HALF_MTR, COL_RMS, color);

    // R Peak
    if (inRange(px, RPEAK_X0, RPEAK_X1) && inRange(py, MEAS_Y0, MEAS_Y1))
        drawMeter(px - RPEAK_X0, py - MEAS_Y0, peakRmsData[2], prHolds[2], HALF_MTR, COL_PEAK, color);

    // R RMS
    if (inRange(px, RRMS_X0, RRMS_X1) && inRange(py, MEAS_Y0, MEAS_Y1))
        drawMeter(px - RRMS_X0, py - MEAS_Y0, peakRmsData[3], prHolds[3], HALF_MTR, COL_RMS, color);

    FragColor = color;
}
)";
//glsl logic:
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
