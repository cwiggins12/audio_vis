#pragma once

#include "hold_value.h"
#include "smooth_value.h"
#include "audio.h"
#include "audio_spec.h"
#include <cstdint>

static constexpr float MIN_FREQ = 20.0f;
static constexpr float MID_FREQ = 1000.0f;
static constexpr float SWAP_FREQ = 2000.0f;
static constexpr float MAX_FREQ = 20000.0f;
static constexpr float MIN_DB = -96.0f;
//NOTE: initial window settings here for now, will move once things are up and running
static constexpr float INIT_WIDTH = 1280.0f;
static constexpr float INIT_HEIGHT = 720.0f;

class AVBridge {
public:
    AVBridge(Audio& a, AudioSpec& spec) : audio(a), currSpec(spec) {}
    ~AVBridge() {}
    AVBridge(const AVBridge&) = delete;
    AVBridge& operator=(const AVBridge&) = delete;
    AVBridge(AVBridge&&) = delete;
    AVBridge& operator=(AVBridge&&) = delete;

    void init(uint32_t deviceFrameRate) {
        frameRate = deviceFrameRate;
        binAmt = audio.getFFTSize() / 2 + 1;
        channels = audio.getNumChannels();
        sampleRate = audio.getSampleRate();
        swap(currSpec);
    }

    void nextFrame() {
        //TODO: would adding if/else here for calls where smoothing is disabled call next over asym
        gpuPeakRMS.advanceAllAsym();
        gpuFFT.advanceAllAsym();
        fftHolds.countdownAll();
        peakRMSHolds.countdownAll();
    }

    const float* getFFTPtr() {
        return gpuFFT.getCurrents();
    }

    const float* getPeakRMSPtr() {
        return gpuPeakRMS.getCurrents();
    }

    const float* getFFTHoldPtr() {
        return fftHolds.getValuePtr();
    }

    const float* getPeakRMSHoldPtr() {
        return peakRMSHolds.getValuePtr();
    }

    void resize(int w, int h) {
        if (currSpec.isSizeWidthDependent && currSpec.isSizeHeightDependent) {
            //TODO: wtf should I do here :(
        }
        else if (currSpec.isSizeWidthDependent) {
            w *= widthScalar;
            setIndexFreqs(w);
            gpuFFTSize = w;
            gpuFFT.resize(w);
            if (currSpec.getsFFTHolds) {
                float minVal = (currSpec.isFFTdB) ? MIN_DB : 0.0f;
                fftHolds.resize(w);
            }
            gpuPeakRMS.setAllCurrentAndTargets(0.0f);
            peakRMSHolds.clear();
        }
        else if (currSpec.isSizeHeightDependent) {
            h *= heightScalar;
            setIndexFreqs(h);
            gpuFFTSize = h;
            gpuFFT.resize(h);
            if (currSpec.getsFFTHolds) {
                float minVal = (currSpec.isFFTdB) ? MIN_DB : 0.0f;
                fftHolds.resize(h);
            }
            gpuPeakRMS.setAllCurrentAndTargets(0.0f);
            peakRMSHolds.clear();
        }
    }

    void swapSpec(AudioSpec& newSpec) {
        swap(newSpec);
        currSpec = newSpec;
    }

    size_t getFFTGPUSize() {
        return gpuFFTSize * sizeof(float);
    }

    size_t getPeakRMSGPUSize() {
        size_t size = (currSpec.isPeakRMSMono) ? 2 : channels * 2;
        return size * sizeof(float);
    }

    //pls refactor
    void formatData() {
        //make peak/rms a helper pls
        const bool prMono = currSpec.isPeakRMSMono;
        const bool getPRHolds = currSpec.getsPeakRMSHolds;
        const bool isPRdB = currSpec.isPeakRMSdB;

        float peak = audio.popPeak(0);
        float rms = audio.popRMS(0);
        if (isPRdB) {
            peak = gainToDB(peak, MIN_DB);
            rms = gainToDB(rms, MIN_DB);
        }
        if (getPRHolds) {
            peakRMSHolds.compareValAtIndex(0, peak);
            peakRMSHolds.compareValAtIndex(1, rms);
        }
        gpuPeakRMS.setTargetVal(0, peak);
        gpuPeakRMS.setTargetVal(1, rms);

        if (!prMono) {
            for (int ch = 1; ch < channels; ++ch) {
                peak = audio.popPeak(ch);
                rms = audio.popRMS(ch);
                int pInd = ch * 2;
                int rInd = ch * 2 + 1;
                if (isPRdB) {
                    peak = gainToDB(peak, MIN_DB);
                    rms = gainToDB(rms, MIN_DB);
                }
                if (getPRHolds) {
                    peakRMSHolds.compareValAtIndex(pInd, peak);
                    peakRMSHolds.compareValAtIndex(rInd, rms);
                }
                gpuPeakRMS.setTargetVal(pInd, peak);
                gpuPeakRMS.setTargetVal(rInd, rms);
            }
        }

        if (currSpec.useAudibleSize) {
            audibleBinPlacement();
        }
        else if (currSpec.arbitrarySize == 0) {
            fullBinPlacement();
        }
        else {
            linearPlacement();
        }
        if (currSpec.getsFFTHolds) {
            for (int i = 0; i < gpuFFTSize; ++i) {
                fftHolds.compareValAtIndex(i, gpuFFT.getCurrentVal(i));
            }
        }
    }

private:
    void swap(AudioSpec& newSpec) {
        fftSize = audio.getFFTSize();

        uint32_t peakRMSSize = (newSpec.isPeakRMSMono) ? 2 : channels * 2;

        if (newSpec.getsPeakRMSHolds) {
            float min = newSpec.isPeakRMSdB ? MIN_DB : 0.0f;
            peakRMSHolds.reset(frameRate, newSpec.peakRMSHoldTime, 0.975f, min);
            peakRMSHolds.resize(peakRMSSize);
        }
        gpuPeakRMS.resize(peakRMSSize);

        if (newSpec.isSizeWidthDependent && newSpec.isSizeHeightDependent) {
            //TODO: figure how tf to get the factor to affect both in a way thats usable
            widthScalar = INIT_WIDTH / gpuFFTSize;
            heightScalar = INIT_HEIGHT / gpuFFTSize;
        }
        else if (newSpec.isSizeWidthDependent) {
            widthScalar = INIT_WIDTH / gpuFFTSize;

        }
        else if (newSpec.isSizeHeightDependent) {
            heightScalar = INIT_HEIGHT / gpuFFTSize;
        }

        audio.getAudibleRange(&audibleStart, &audibleSize);
        if (newSpec.arbitrarySize == 0 && !newSpec.useAudibleSize) {
            gpuFFTSize = binAmt;
        }
        else if (newSpec.arbitrarySize == 0 && newSpec.useAudibleSize) {
            gpuFFTSize = audibleSize;
        }
        else {
            const bool h = newSpec.isSizeHeightDependent && currentHeight != INIT_HEIGHT;
            const bool w = newSpec.isSizeWidthDependent && currentWidth != INIT_WIDTH;
            if (h && w) {
                //TODO: figure out this too pls.....
            }
            else if (h) {
                gpuFFTSize = newSpec.arbitrarySize * heightScalar;
            }
            else if (w) {
                gpuFFTSize = newSpec.arbitrarySize * widthScalar;
            }
            else {
                gpuFFTSize = newSpec.arbitrarySize;
            }
            setIndexFreqs(gpuFFTSize);
        }
        gpuFFT.resize(gpuFFTSize);

        if (newSpec.getsFFTHolds) {
            float min = newSpec.isFFTdB ? MIN_DB : 0.0f;
            fftHolds.reset(frameRate, newSpec.fftHoldTime, 0.975f, min);
            fftHolds.resize(gpuFFTSize);
        }
        if (!newSpec.useFFTSmoothing) {
            gpuFFT.reset(0);
            gpuFFT.setAsym(1,1);
        }
        else {
            gpuFFT.reset(frameRate);
            gpuFFT.setAsym(newSpec.fftAtk, newSpec.fftRls);
        }

        if (!newSpec.usePeakRMSSmoothing) {
            gpuPeakRMS.reset(0);
            gpuPeakRMS.setAsym(1,1);
        }
        else {
            gpuPeakRMS.reset(frameRate);
            gpuPeakRMS.setAsym(newSpec.fftAtk, newSpec.fftRls);
        }
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below bin collating algo
    void setIndexFreqs(uint32_t size) {
        indexFreqs.resize(size);
        float swapFreq = 1000; //needs math to find swapFreq
        bool swapIndexFound = false;
        const float scale = (float)fftSize / (float)sampleRate;

        for (uint32_t i = 0; i < size; ++i) {
            float norm = (float)i / (float)(size - 1);
            float freq = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, norm);
            if (!swapIndexFound && freq > swapFreq) {
                swapIndex = i;
                swapIndexFound = true;
            }
            float binIndexFloat = freq * scale;

            indexFreqs[i] = std::min(std::max(binIndexFloat, 0.0f), (float)binAmt);
        }
    }

    //gpuing based on arbitrary size for the purpose of per pixel gpuing
    //choose a freq as midpoint in const expr above, then cubic interp up to that point
    //will get non overlapping rms of each bucket after midpoint, places in gpu as dB
    void linearPlacement() {
        const float* fftOut = audio.getFFTPtr();
        //now that swap index is saved, this can be two loops, easier to vectorize for comp(or by hand if need arises)
        //TODO: look for a better way to handle isDB being false
        if (currSpec.isFFTdB) {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, getLowFreqValue(i, fftOut));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, getHighFreqValueDB(i, fftOut));
            }
        }
        else {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, dBToGain(getLowFreqValue(i, fftOut), MIN_DB));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, getHighFreqValueGain(i, fftOut));
            }
        }
    }

    //get low end interp using surrounding bins, since they are sparse here
    float getLowFreqValue(int i, const float* fftOut) {
        float centerBinFloat = indexFreqs[i];
        uint32_t bin1 = (int)centerBinFloat;
        float fraction = centerBinFloat - bin1; 

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0];
        float y1 = fftOut[bin1];
        float y2 = fftOut[bin2];
        float y3 = fftOut[bin3];

        return cubicInterp(y0, y1, y2, y3, fraction);
    }

    //basically, gets rms of bounds from last bucket + 1 to current
    float getHighFreqValueDB(int idx, const float* fftOut) {
        int lowB = (int)indexFreqs[idx - 1] + 1;
        int highB = (int)indexFreqs[idx];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float mag = dBToGain(fftOut[i], MIN_DB);
            sumSq += mag * mag;
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return gainToDB(rms, MIN_DB);
    }

    //expects gain and returns gain
    float getHighFreqValueGain(int idx, const float* fftOut) {
        int lowB = (int)indexFreqs[idx - 1] + 1;
        int highB = (int)indexFreqs[idx];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float mag = fftOut[i];
            sumSq += mag * mag;
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return rms;
    }

    //fft gpu low end helper to cubic interpolate
    float cubicInterp(float y0, float y1, float y2, float y3, float mu) {
        //Catmull-Rom spline interpolation
        float mu2 = mu * mu;
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 = y1;

        return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
    }

    //if not using pixel gpuing array, this is the bin only alternative
    void audibleBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        float* gpu = gpuFFT.getTargets();
        for (uint32_t i = 0; i < audibleSize; ++i) {
            gpu[i] = fftPtr[i + audibleStart];
        }
    }

    void fullBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        float* gpu = gpuFFT.getTargets();
        for (uint32_t i = 0; i < binAmt; ++i) {
            gpu[i] = fftPtr[i];
        }
    }

    //float to float gain/mag to dB helper
    float gainToDB(float gain, float minDB) {
        return std::max(minDB, 20.0f * std::log10(gain));
    }

    //float to float db to gain/mag helper
    float dBToGain(float dB, float minDB) {
        dB = std::max(minDB, dB);
        return std::pow(10.0f, dB * 0.05f);
    }

    Audio& audio;
    AudioSpec currSpec;

    SmoothArraySoA gpuPeakRMS;
    SmoothArraySoA gpuFFT;
    HoldArray fftHolds;
    HoldArray peakRMSHolds;

    std::vector<float> indexFreqs;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;
    uint32_t hopAmt = 0;
    uint32_t binAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t frameRate = 0;

    uint32_t audibleStart = 0;
    uint32_t audibleSize = 0;

    uint32_t gpuFFTSize = 0;
    uint32_t swapIndex = 0;

    int currentWidth = 0;
    int currentHeight = 0;
    float widthScalar = 0.0f;
    float heightScalar = 0.0f;
};

