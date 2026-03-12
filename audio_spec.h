#pragma once

#include <cstdint>

//for right now the fft_size is static, if I see any demand for a change I'll debate it
//it would be really expensive for very little gain that I know of
struct AudioSpec {
    //TODO: these all need comments to explain how they can affect the data and why
    uint32_t hopAmt = 4;
    uint32_t arbitrarySize = 1000;
    uint32_t fftAtk = 5;
    uint32_t fftRls = 1;
    uint32_t rmsPeakAtk = 5;
    uint32_t rmsPeakRls = 1;

    //if using smooth size, does it resize with window in scale
    bool isSizeWindowDependent = true;
    //want smoothing for fft ouput?
    bool useFFTSmoothing = true;
    //want fft output limited to only audible bins?
    bool useAudibleSize = false;
    //smooth rms/peak output?
    bool usePeakRMSSmoothing = true;

    bool getsPeakHolds = true;
    bool isPeakMono = false;
    bool isRMSMono = false;
    bool getsFFTHolds = true;

    bool isPerceptual = true;
    bool isHannWindowed = true;
    bool isDB = true;
    bool isSingleSided = true;

    float slope = 4.5;
};

