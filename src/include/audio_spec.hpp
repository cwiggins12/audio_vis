#pragma once

#include <cstdint>
#include <map>
#include <string>

enum LowMode {
    LINEAR,         //0. fine and cheap
    PCHIP,          //1. mid
    LANCZOS,        //2. kinda hate
    GAUSSIAN,       //3. cool but boosts area around peaks a lot
    CUBIC_B,        //4. cool and good peaks
    AKIMA,          //5. not sure if necessary with the others, but looks good
    HERMITE,        //6. not sure on difference between catmull rom
    STEFFEN,        //7. all around fine, kind looks like some others though
    CATMULL_ROM_3,  //8. a classic, and not too pricey
};

enum HighMode {
    RMS,        //0. cool
    PEAK,       //1. cool
    POWER_MEAN, //3. cool
    L_NORM,     //4. needs fixing
};

//now that this includes feedback ssbos, AudioSpec is no longer a great name. Change l8r
struct AudioSpec {
    //for custom sized pixel aligned fftOutputs, if non-zero this will
    //give a log scaled bin output for frequency to align with pixels
    //in a way that lines up with how we hear. It will cubic interpolate
    //until a dynamically found point shows that each pixel has at least 1 bin
    //from there it will rms each bin within a pixel's bounds.
    //If you don't want to deal with freq space, this entirely abstracts it away :)
    uint32_t customSize = 1000;
    HighMode highMode = RMS;
    LowMode lowMode = GAUSSIAN;
    //if useFFTSmoothing == true, these will decide the attack and release values 
    //for asymmetrical smoothing. The values are in seconds and will be dropped to
    //the last frame shown before this time
    //ie. device = 30 fps, fftAtk = .35, this will go from current value to target value
    //upward in 3 frames. Generally, audio folks like a lower attack than release
    //if you set to 0.0, no smoothing will occur, even if the bool is true
    //if you set to negative, it will set it to 1 second as a fallback (don't do that)
    float fftAtk = 0.05f;
    float fftRls = 0.3f;
    //same logic for peak/RMS meters as the fft 
    float peakRMSAtk = 0.05f;
    float peakRMSRls = 0.3f;
    //Hold time value defines how long a peak is held in seconds before dropping
    float peakRMSHoldTime = 1.0f;
    float fftHoldTime = 1.0f;
    //exponential scale by which the holds drop after the hold time ends
    float peakRMSHoldScalar = 0.975f;
    float fftHoldScalar = 0.975f;
    //if isFFTPerceptual is true, it will use this slope
    //common values are 3.0 for a flat pink noise, 0.0 for flat white noise,
    //or 4.5 for a more music focused analyser popularized by FabFilter's Pro-Q
    float perceptualSlopeDegrees = 3.0f;

    //currently, only one of these should be true at once
    //there will be more ways to work with this later, but right now
    //whichever one is true, when resized, will be compared against the 
    //original dimension, then that scalar will multiply the bin amount
    //this only affects custom bin amount in this version
    bool isSizeWidthDependent = false;
    bool isSizeHeightDependent = false;

    //want atk and rls over time smoothing for fft ouput?
    bool useFFTSmoothing = true;
    //if using customLogSize, do you want the high end rms bins within index (true) or peak metering bins within index
    bool isHighEndRMS = true;
    //If custom size is 0 and you just want the audible bins (20Hz-20kHz)
    //the amount will be given in the numBins uniform
    bool useAudibleSize = false;
    //want fft holds? rarely worth it tbh, and take up quite a bit of resources
    //will give an array the same size as the fft ouput array(given in numBins)
    bool getsFFTHolds = true;
    //toggles perceptual slope for fft output
    bool isPerceptual = true;
    //adds a windowing function with normalization to the fft output
    bool isHannWindowed = true;
    //outputs fft output in db rather than 0 to 1 amplitude value.
    //generally, if you are looking for something to map more clearly to a
    //linear space similar to how we hear it, get dB then map to pixels
    bool isFFTdB = true;

    //these do the same as the fft options, but for peak and RMS values.
    bool usePeakRMSSmoothing = true;
    bool isPeakRMSdB = true;
    bool getsPeakRMSHolds = true;
    bool isPeakRMSMono = false;

    //if you want a buffer to cycle for feedback loops in your shader,
    //define its size here. This will result in 2 ssbos of the specified size
    //due to needing to double buffer for an in and out buffer.
    //shader will read from feedbackIn and write to feedbackOut each frame
    uint32_t feedbackBufferSize = 0;
    //0 is off, 1 is width only, 2 is height only, 3 is resolution scaling
    uint32_t feedbackBufferScalesWithWindow = 0;

    //if you want an initial value to the buffer elements. Set it here.
    float feedbackBufferInitValue = 0.0f;
    std::map<std::string, std::string> textures;
};

