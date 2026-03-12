#pragma once

#include "smooth_value.h"
#include "audio.h"
#include "audio_spec.h"

static constexpr float MIN_FREQ = 20.0f;
static constexpr float MID_FREQ = 1000.0f;
static constexpr float SWAP_FREQ = 2000.0f;
static constexpr float MAX_FREQ = 20000.0f;
static constexpr float MIN_DB = -96.0f;

class AVBridge {
public:
    AVBridge(Audio& a, AudioSpec& spec) : audio(a), currSpec(spec) {}
/*
    AVBridge(Audio& a, uint32_t hopAmt, uint32_t smoothSize, bool isWindowDepenedent, bool useSmoothing, bool useAudibleSize, bool peakHolds, 
             bool peakMono, bool rmsMono, bool fftHolds, bool isPerceptual, 
             bool isHannWindowed, bool isDB, bool isSingleSided, float slope)
             : audio(a) {
        currSpec = {hopAmt, smoothSize, isWindowDepenedent, useSmoothing, useAudibleSize, peakHolds, peakMono, rmsMono, fftHolds,
                    isPerceptual, isHannWindowed, isDB, isSingleSided, slope};
    }
*/
    ~AVBridge() {}
    AVBridge(const AVBridge&) = delete;
    AVBridge& operator=(const AVBridge&) = delete;
    AVBridge(AVBridge&&) = delete;
    AVBridge& operator=(AVBridge&&) = delete;

    bool init(uint32_t deviceFrameRate) {
        if (!audio.init(currSpec)) {
            return false;
        }

        frameRate = deviceFrameRate;
        binAmt = audio.getFFTSize() / 2 + 1;
        channels = audio.getNumChannels();
        sampleRate = audio.getSampleRate();
        audibleRange.resize(2);

        swap(currSpec);

        return true;
    }

    void nextFrame() {
        smoothPeakRMS.advanceAllAsym();
        smoothFFT.advanceAllAsym();
    }

    const float* getSmoothFFTPtr() {
        return smoothFFT.getCurrents();
    }

    const float* getPeakRMSPtr() {
        return smoothPeakRMS.getCurrents();
    }

    void resize(size_t newSize) {
        setSmoothIndexFreqs(newSize);
        smoothFFTSize = newSize;
        smoothFFT.resize(newSize);
        //either separate call to Audio or call it here
        //firstWindowAccumulated = false;
    }

    void swapSpec(AudioSpec& newSpec, uint32_t deviceFrameRate) {
        audio.swapSpec(newSpec);
        swap(newSpec);
    }

    uint32_t getFFTGPUSize() {
        return fftGPUSize * sizeof(float);
    }

    uint32_t getPeakRMSGPUSize() {
        uint32_t size = 0;
        size += (currSpec.isPeakMono) ? 1 : channels;
        size += (currSpec.isRMSMono) ? 1 : channels;
        return size * sizeof(float);
    }

    void formatData() {

    }

private:
    void swap(AudioSpec& newSpec) {
        audibleRange = audio.getAudibleRange();
        if (newSpec.arbitrarySize == 0 && !newSpec.useAudibleSize) {
            fftGPUSize = binAmt;
        }
        else if (newSpec.arbitrarySize == 0 && newSpec.useAudibleSize) {
            fftGPUSize = audibleRange[1];
        }
        else {
            fftGPUSize = newSpec.arbitrarySize;
            setSmoothIndexFreqs(fftGPUSize);
        }
        smoothFFT.resize(fftGPUSize);
        smoothPeakRMS.resize(channels);

        if (currSpec.useFFTSmoothing) {
            smoothFFT.reset(0);
            smoothFFT.setAsym(1,1);
        }
        else {
            smoothFFT.reset(frameRate);
            smoothFFT.setAsym(currSpec.fftAtk, currSpec.fftRls);
        }

        if (currSpec.usePeakRMSSmoothing) {
            smoothPeakRMS.reset(0);
            smoothPeakRMS.setAsym(1,1);
        }
        else {
            smoothPeakRMS.reset(frameRate);
            smoothPeakRMS.setAsym(currSpec.fftAtk, currSpec.fftRls);
        }

        audio.resetAccumulator();
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below smoothing algo
    void setSmoothIndexFreqs(size_t size) {
        smoothIndexFreqs.resize(size);
        float swapFreq = 0.0f; //needs math to find swapFreq
        bool swapIndexFound = false;
        const float scale = (float)fftSize / (float)sampleRate;

        for (int i = 0; i < size; ++i) {
            float norm = (float)i / (float)(size - 1);
            float freq = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, norm);
            if (!swapIndexFound && freq > swapFreq) {
                swapIndex = i;
                swapIndexFound = true;
            }
            float binIndexFloat = freq * scale;

            smoothIndexFreqs[i] = std::min(std::max(binIndexFloat, 0.0f), (float)binAmt);
        }
    }

    //smoothing based on arbitrary size for the purpose of per pixel smoothing
    //choose a freq as midpoint in const expr above, then cubic interp up to that point
    //will get non overlapping rms of each bucket after midpoint, places in smooth as dB
    void spencySmooth() {
        const float* fftOut = audio.getFFTPtr();
        //now that swap index is saved, this can be two loops, easier to vectorize for comp(or by hand if need arises)
        for (int i = 0; i < smoothFFTSize; ++i) {
            float dB;

            if (i < swapIndex) {
                dB = getLowFreqSmoothedValue(i, fftOut);
            }
            else {
                dB = getHighFreqSmoothedValues(i, fftOut);
            }
            smoothFFT.setTargetVal(i, dB);
        }
    }

    //get low end interp using surrounding bins, since they are sparse here
    float getLowFreqSmoothedValue(int i, const float* fftOut) {
        float centerBinFloat = smoothIndexFreqs[i];
        uint32_t bin1 = (int)centerBinFloat;
        float fraction = centerBinFloat - bin1; 

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt, bin1 + 2);

        float y0 = fftOut[bin0];
        float y1 = fftOut[bin1];
        float y2 = fftOut[bin2];
        float y3 = fftOut[bin3];

        return cubicInterp(y0, y1, y2, y3, fraction);
    }

    //basically, gets rms of bounds from last bucket + 1 to current
    float getHighFreqSmoothedValues(int idx, const float* fftOut) {
        int lowB = (int)smoothIndexFreqs[idx - 1] + 1;
        int highB = (int)smoothIndexFreqs[idx];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float mag = dBToGain(fftOut[i], MIN_DB);
            sumSq += mag * mag;
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return gainToDB(rms, MIN_DB);
    }

    //pixel smoothing low end helper to cubic interpolate
    float cubicInterp(float y0, float y1, float y2, float y3, float mu) {
        //Catmull-Rom spline interpolation
        float mu2 = mu * mu;
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 = y1;

        return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
    }

    //if not using pixel smoothing array, this is the bin only alternative
    void binToSmooth() {
        const float* fftPtr = audio.getFFTPtr();
        float* smooth = smoothFFT.getTargets();
        for (int i = 0; i < audibleRange[1]; ++i) {
            smooth[i] = fftPtr[i + audibleRange[0]];
        }
    }

    //float to float gain/mag to dB helper
    float gainToDB(float mag, float minDB) {
        return std::max(minDB, 20.0f * std::log10(mag));
    }

    //float to float db to gain/mag helper
    float dBToGain(float dB, float minDB) {
        dB = std::max(minDB, dB);
        return std::pow(10.0f, dB * 0.05f);
    }

    Audio& audio;
    AudioSpec currSpec;

    SmoothArraySoA smoothPeakRMS;
    //TODO: these need renaming pls
    SmoothArraySoA smoothFFT;
    std::vector<float> smoothIndexFreqs;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;
    uint32_t hopAmt = 0;
    uint32_t binAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t frameRate = 0;

    std::vector<uint32_t> audibleRange; 
    size_t smoothFFTSize;
    uint32_t swapIndex = 0;
    uint32_t fftGPUSize = 0;
};

