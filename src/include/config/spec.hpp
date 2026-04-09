#pragma once

#include <cstdint>
#include <map>
#include "config/expr_eval.hpp"

enum Interps {
    LINEAR,
    PCHIP,
    LANCZOS,
    GAUSSIAN,
    CUBIC_B,
    AKIMA,
};

enum Collates {
    RMS,
    PEAK,
    POWER_MEAN,
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

enum FFTMeasurement {
    POWER,
    MAGNITUDE,
    DECIBELS,
};


struct Spec {
    //0 = full direct bin amt(no high/low mode processing), 1 = audbileBins only,
    //2 = customFFTSize related output
    //Amount sent each frame will always be passed to the fftArrSize uniform
    FFTOutputMode fftOutputMode = CUSTOM_SIZE;
    //for custom sized pixel aligned fftOutputs.
    //If you don't want to deal with freq space, this entirely abstracts it away :)
    uint32_t customFFTSize = 0;
    //this is not used by a user, this check for usage of ExprVariable's in the input
    //string, then the system can check this to see if the size needs an update
    //on change of this var, which is what the bitset signals
    std::string customFFTSizeExpr = "";
    std::bitset<EXPR_VAR_AMT> fftUsesExprVar{};
    //0 is no scaling, 1 is width only, 2 is height only, 3 is resolution
    //DONT SET THIS TO TRUE WHEN USING A HEIGHT, WIDTH, OR RESOLUTION VAR IN THE
    //EXPRESSION OR IT WILL DOUBLY SCALE ON WINDOW CHANGES. THIS ONLY EXISTS FOR CASES
    //WHEN THE USER WANTS WINDOW SCALING, BUT DOESN'T WANT TO USE THE VARIABLES
    WindowScalingMode customFFTSizeScalesWithWindow = NO_SCALE;
    //collates and interps listed above, interp sparse bins
    //mode will switch to high dynamically based on the point where each index will
    //have at least one bin. A second pass on the high end is available to further
    //smooth it to match the low end below, and is optional
    Collates highMode = RMS;
    Interps lowMode = GAUSSIAN;
    //outputs fft output in db rather than 0 to 1 amplitude value.
    //generally, if you are looking for something to map more clearly to a
    //linear space similar to how we hear it, get dB then map to pixels
    //0 is power, 1 is magnitude, and 2 is decibels
    FFTMeasurement fftOutputMeasurement = DECIBELS;
    //if useFFTSmoothing == true, these will decide the attack and release values
    //for asymmetrical smoothing. The values are in seconds and will be dropped to
    //the last frame shown before this time
    //ie. device = 30 fps, fftRls = .3, this will go from current value to target value
    //in 3 frames. Generally, audio folks like a lower attack than release
    //if you set to 0.0, no smoothing will occur, even if the bool is true
    //if you set to negative, it will set it to 0
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

    //want atk and rls over time smoothing for fft ouput? Highly recommended
    bool useFFTSmoothing = true;
    //want fft holds?
    //will give an array the same size as the fft ouput array(given in numBins)
    bool getFFTHolds = true;
    //adds a windowing function with normalization to the fft output
    bool isFFTHannWindowed = true;

    //if you want the samples from each hop for a waveform or something.
    //Currently, mono sum of channels only. Same data fft gets, but just the new hop
    bool getRawSamples = true;

    //these do the same as the fft options, but for peak and RMS values.
    bool usePeakRMSSmoothing = true;
    bool getPeakRMSHolds = true;
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
    //the backend will handle all of the ping pong logic
    uint32_t feedbackBufferSize = 0;
    //same custom sizing logic as the custom fft size
    std::string feedbackBufferSizeExpr = "";
    std::bitset<EXPR_VAR_AMT> feedbackUsesExprVar{};
    //0 is off, 1 is width only, 2 is height only, 3 is resolution scaling
    WindowScalingMode feedbackBufferScalesWithWindow = NO_SCALE;
    //if you want an initial value to the buffer elements. Set it here.
    float feedbackBufferInitValue = 0.0f;

    std::map<std::string, std::string> textures;
};

