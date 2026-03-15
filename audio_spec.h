#pragma once

#include <cstdint>

//current gripes: 
//arbitrarySize is a bad name, 
//hopAmt shouldn't be here unless fftSize is here, which would be a nightmare
//atk and rls values need a clear explanation or need to be changed
//slope is not a descriptive enough name
//
//data that needs to be passed aside from this is channel count and, 
//when not using the customFFTSize, the bin amt they are getting passed

//for right now the fft_size is static, if I see any demand for a change I'll debate it
//it would be really expensive for very little gain that I know of
struct AudioSpec {
    //TODO: these all need comments to explain how they can affect the data and why

    uint32_t hopAmt = 2;
    uint32_t arbitrarySize = 1000;
    uint32_t fftAtk = 20;
    uint32_t fftRls = 5;
    uint32_t rmsPeakAtk = 20;
    uint32_t rmsPeakRls = 5;
    float peakRMSHoldTime = 1.0f;
    float fftHoldTime = 1.0f;

    float hFactor = 1.0f;
    float wFactor = 1.0f;

    //if using arbitrary size, does it resize with window width in scale
    bool isSizeWidthDependent = false;
    bool isSizeHeightDependent = false;
    //want smoothing for fft ouput?
    bool useFFTSmoothing = true;
    //want fft output limited to only audible bins?
    bool useAudibleSize = false;

    //smooth rms/peak output?
    bool usePeakRMSSmoothing = true;
    bool isPeakRMSdB = true;
    bool getsPeakRMSHolds = true;
    bool isPeakRMSMono = false;

    bool getsFFTHolds = true;
    bool isPerceptual = true;
    bool isHannWindowed = true;
    bool isFFTdB = true;

//need something to account for both height and width dependencies, a factor? 2 scalars? idk
    float perceptualSlopeDegrees = 4.5;
};

