#pragma once

#include <cstddef>
#include <cmath>
#include <array>


struct Globals {
    float mouseX = 0;  //input handler DONE
    float mouseY = 0;  //input handler DONE
    int time = 0;    //in main DONE
    int W = 0;       //glfw context DONE
    int H = 0;       //glfw context DONE
    int mouseDown = 0; //input handler DONE

    int fftSize = 0;    //set in AudioSystem DONE
    int hopSize = 0;    //set in AudioSystem DONE
    int fftBinAmt = 0;  //set in AudioSystem DONE

    int fftArrSize = 0; //handle within avbridge (could move some of the resize functions to this struct) DONE

    int newAudioWindow = 0; //in main DONE
    int numChannels = 0; //get from capture DONE
    int displayHz = 0; //glfw context DONE
    int sampleRate = 0; //get from capture DONE

    int showError = 0; //set these 3 in shader system swap
    int errorLen = 0;  //DONE
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

inline constexpr float MIN_FREQ = 20.0f;
inline constexpr float MAX_FREQ = 20000.0f;
inline constexpr float MIN_DB = -96.0f;
inline constexpr int   MAX_FFT_SIZE = 8192;
inline constexpr size_t globalsGPUSize = offsetof(Globals, initWidth);

