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
    float peakRMSHoldTime = 1.0f;
    float fftHoldTime = 1.0f;

    //if height and width both affect arbitrary size and you want a different scale
    //of effect from height and width. 
    //Values > 1 emphasize height, and < 1 emphasize width more by the percent away from 1
    float hwFactor = 1.0f;

    //if using arbitrary size, does it resize with window width in scale
    bool isSizeWidthDependent = true;
    bool isSizeHeightDependent = false;
    //want smoothing for fft ouput?
    bool useFFTSmoothing = true;
    //want fft output limited to only audible bins?
    bool useAudibleSize = false;

    //smooth rms/peak output?
    bool usePeakRMSSmoothing = true;
    bool isRMSdB = true;
    bool isPeakdB = true;

    bool getsPeakRMSHolds = true;
    bool isPeakMono = false;
    bool isRMSMono = false;
    bool getsFFTHolds = true;

    bool isPerceptual = true;
    bool isHannWindowed = true;
    bool isFFTdB = true;
    bool isSingleSided = true;
//need something to account for both height and width dependencies, a factor? 2 scalars? idk
    float slope = 4.5;
};

