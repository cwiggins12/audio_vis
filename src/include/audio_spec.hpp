#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <bitset>
#include "expr_eval.hpp"

enum Interps {
    LINEAR,         //0. fine, jagged, and cheap
    PCHIP,          //1. mid all around
    LANCZOS,        //2. acquired taste for sure. I kinda h8 it lmao
    GAUSSIAN,       //3. cool but boosts area around peaks a lot
    CUBIC_B,        //4. cool and good peaks
    AKIMA,          //5. not sure if necessary with the others, but looks good
    STEFFEN,        //6. all around fine, kind looks like some others though
    CATMULL_ROM_3,  //7. a classic, and not too pricey
};

enum Collates {
    RMS,        //0. cool
    PEAK,       //1. cool
    POWER_MEAN, //3. cool
    L_NORM,     //4. needs testing
};

enum FFTOutputMode {
    FULL_BIN,
    AUDIBLE_BIN,
    CUSTOM_SIZE,
};

enum WindowScalingMode {
    NO_SCALE,
    WIDTH_SCALE,
    HEIGHT_SCALE,
    RESOLUTION_SCALE
};

enum SecondPassMode {
    NO_SECOND_PASS,
    USE_LOW_END_INTERP,
    USE_SEPARATE_INTERP
};

enum FFTMeasurement {
    POWER,
    MAGNITUDE,
    DECIBELS,
};

//reorder this to save a bit of space later on. User shouldn't need this file for docs
struct Spec {
    //0 = full direct bin amt(no high/low mode processing), 1 = audbileBins only,
    //2 = customFFTSize related output
    //Amount sent each frame will always be passed to the numBins uniform
    FFTOutputMode fftOutputMode = FULL_BIN;
    //for custom sized pixel aligned fftOutputs.
    //If you don't want to deal with freq space, this entirely abstracts it away :)
    uint32_t customFFTSize = 1000;
    std::string customFFTSizeExpr = "";
    //this is not used by a user, this check for usage of ExprVariable's in the above string
    //then the system can check this to see if the size needs an update on change of this var
    std::bitset<EXPR_VAR_AMT> fftUsesExprVar{};
    //0 is no scaling, 1 is width only, 2 is height only, 3 is resolution
    WindowScalingMode customFFTSizeScalesWithWindow = NO_SCALE;
    //collates and interps listed above, interp sparse bins
    //mode will switch to high dynamically based on the point where each index will
    //have at least one bin. A second pass on the high end is available to further
    //smooth it to match the low end below, and is optional
    Collates highMode = RMS;
    Interps lowMode = GAUSSIAN;
    //if, after the first high mode run, you want it as smooth as the low end
    //0 = no high end second pass
    //1 = high end uses current low end interpolation strategy, 
    //2 = uses separate chosen interp chosen below
    SecondPassMode highSecondPassMode = NO_SECOND_PASS;
    Interps highSecondPassInterp = GAUSSIAN;
    //outputs fft output in db rather than 0 to 1 amplitude value.
    //generally, if you are looking for something to map more clearly to a
    //linear space similar to how we hear it, get dB then map to pixels
    //0 is power, 1 is magnitude, and 2 is decibels
    //if using custom size, only decibels are available at the moment.
    FFTMeasurement fftOutputMeasurement = DECIBELS;
    //if useFFTSmoothing == true, these will decide the attack and release values 
    //for asymmetrical smoothing. The values are in seconds and will be dropped to
    //the last frame shown before this time
    //ie. device = 30 fps, fftAtk = .35, this will go from current value to target value
    //upward in 3 frames. Generally, audio folks like a lower attack than release
    //if you set to 0.0, no smoothing will occur, even if the bool is true
    //if you set to negative, it will set it to 1 second as a fallback (don't do that)
    float fftAtk = 0.05f;
    float fftRls = 0.3f;
    //Hold time value defines how long a peak is held in seconds before dropping
    float fftHoldTime = 1.0f;
    //exponential scale by which the holds drop after the hold time ends
    float fftHoldScalar = 0.975f;
    //if isFFTPerceptual is true, it will use this slope
    //common values are 3.0 for a flat pink noise, 0.0 for flat white noise,
    //or 4.5 for a more music focused analyser popularized by FabFilter's Pro-Q
    //if value is 0, no slope is applied
    float perceptualSlopeDegrees = 3.0f;

    //want atk and rls over time smoothing for fft ouput?
    bool useFFTSmoothing = true;
    //want fft holds? rarely worth it tbh, and take up quite a bit of resources
    //will give an array the same size as the fft ouput array(given in numBins)
    bool getsFFTHolds = true;
    //adds a windowing function with normalization to the fft output
    bool isFFTHannWindowed = true;

    //these do the same as the fft options, but for peak and RMS values.
    bool usePeakRMSSmoothing = true;
    bool getsPeakRMSHolds = true;
    //only able to do magnitude or db for these outputs, affects values and holds
    bool isPeakRMSdB = true;
    bool isPeakRMSMono = false;
    //same logic for peak/RMS meters as the fft
    float peakRMSAtk = 0.05f;
    float peakRMSRls = 0.3f;
    float peakRMSHoldTime = 1.0f;
    float peakRMSHoldScalar = 0.975f;

    //if you want a buffer to cycle for feedback loops in your shader,
    //define its size here. This will result in 2 ssbos of the specified size
    //due to needing to double buffer for an in and out buffer.
    //shader will read from feedbackIn and write to feedbackOut each frame
    uint32_t feedbackBufferSize = 0;
    std::string feedbackBufferSizeExpr = "";
    std::bitset<EXPR_VAR_AMT> feedbackUsesExprVar{};
    //0 is off, 1 is width only, 2 is height only, 3 is resolution scaling, 4 is scaling off of init Values
    WindowScalingMode feedbackBufferScalesWithWindow = NO_SCALE;
    //if you want an initial value to the buffer elements. Set it here.
    float feedbackBufferInitValue = 0.0f;

    std::map<std::string, std::string> textures;
};

