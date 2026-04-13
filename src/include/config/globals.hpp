#pragma once

#include <cstddef>
#include <cmath>
#include <array>


struct Globals {
    float mouseX = 0;
    float mouseY = 0;
    float time = 0;
    float W = 0;
    float H = 0;
    int mouseDown = 0;

    int fftOrder = 0;
    int fftSize = 0;
    int hopAmt = 0;
    int hopSize = 0;
    int fftBinAmt = 0;

    int fftArrSize = 0;

    int newAudioWindow = 0;
    int numChannels = 0;
    int displayHz = 0;
    int sampleRate = 0;

    int showError = 0;
    int errorLen = 0;
    int _pad1 = 0;
    int _pad2 = 0;
    std::array<int,128> errorChars = {0};

    int initWidth = 0;
    int initHeight = 0;

    size_t getSizeFromModeSwitch(size_t size, int mode) {
        switch (mode) {
            case 0: return size;
            case 1: return std::round(size * ((float)W / (float)initWidth));
            case 2: return std::round(size * ((float)H / (float)initHeight));
            case 3: return std::round(size * ((float)H * (float)W)
                                      / ((float)initHeight * (float)initWidth));
            default: return size;
        }
    }
};

inline constexpr float  MIN_FREQ        = 20.0f;
inline constexpr float  MAX_FREQ        = 20000.0f;
inline constexpr float  MIN_DB          = -96.0f;
inline constexpr int    MAX_FFT_SIZE    = 8192;
inline constexpr size_t globalsGPUSize  = offsetof(Globals, initWidth);

