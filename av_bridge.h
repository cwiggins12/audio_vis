#pragma once

#include "hold_value.h"
#include "smooth_value.h"
#include "audio.h"
#include "audio_spec.h"
#include <cstdint>

//probably want to put these in a globals.h at some point
static constexpr float MIN_FREQ = 20.0f;
static constexpr float MID_FREQ = 1000.0f;
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
        gpuPeakRMS.advanceAll();
        gpuFFT.advanceAll();
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

    //TODO:
    void resize(int w, int h) {
        if (currSpec.isSizeWidthDependent && currSpec.isSizeHeightDependent) {
        }
        else if (currSpec.isSizeWidthDependent) {
        }
        else if (currSpec.isSizeHeightDependent) {
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
        else if (currSpec.customLinearSize == 0) {
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
        //set fftSize, probably just need this in init, but its gonna live here for now
        fftSize = audio.getFFTSize();
        //config peak/RMS hold array
        uint32_t peakRMSSize = (newSpec.isPeakRMSMono) ? 2 : channels * 2;
        if (newSpec.getsPeakRMSHolds) {
            peakRMSHolds.reset(frameRate, newSpec.peakRMSHoldTime, newSpec.peakRMSHoldScalar, 
                               newSpec.isPeakRMSdB, peakRMSSize);
        }
        //config peak/RMS smooth array
        float prMin = newSpec.isPeakRMSdB ? MIN_DB : 0.0f;
        if (newSpec.usePeakRMSSmoothing) {
            gpuPeakRMS.reset(frameRate, newSpec.peakRMSAtk, newSpec.peakRMSAtk, peakRMSSize, prMin);
        }
        else {
            gpuPeakRMS.reset(frameRate, 0.0f, 0.0f, 0.0f, peakRMSSize, prMin);
        }
        //TODO: config fft size
        audio.getAudibleRange(&audibleStart, &audibleSize);
        if (newSpec.customLinearSize == 0 && !newSpec.useAudibleSize) {
            gpuFFTSize = binAmt;
        }
        else if (newSpec.customLinearSize == 0 && newSpec.useAudibleSize) {
            gpuFFTSize = audibleSize;
        }
        else {
            //TODO: still need h and w logic pls
            const bool h = newSpec.isSizeHeightDependent;
            const bool w = newSpec.isSizeWidthDependent;
            if (h && w) {
            }
            else if (h) {
            }
            else if (w) {
            }
            else {
                gpuFFTSize = newSpec.customLinearSize;
            }
            setIndexFreqs(gpuFFTSize);
        }
        //config FFT holds
        if (newSpec.getsFFTHolds) {
            peakRMSHolds.reset(frameRate, newSpec.fftHoldTime, newSpec.fftHoldScalar, 
                               newSpec.isFFTdB, gpuFFTSize);
        }
        //config fft smooth array
        float fftMin = newSpec.isFFTdB ? MIN_DB : 0.0f;
        if (newSpec.useFFTSmoothing) {
            gpuPeakRMS.reset(frameRate, newSpec.fftAtk, newSpec.fftRls, gpuFFTSize, fftMin);
        }
        else {
            gpuPeakRMS.reset(frameRate, 0.0f, 0.0f, 0.0f, gpuFFTSize, fftMin);
        }
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below bin collating algo
    void setIndexFreqs(uint32_t size) {
        indexFreqs.resize(size);
        const float scale = (float)fftSize / (float)sampleRate;
        if (!swapFreqSet) {
            setSwapFreq(scale);
        }
        bool swapIndexFound = false;

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

    void setSwapFreq(const float binWidth) {
        const float logRatio = std::log(MAX_FREQ / MIN_FREQ);
        swapFreq = binWidth * (float)(indexFreqs.size() - 1) / logRatio;
        swapFreq = std::min(std::max(swapFreq, MIN_FREQ), MAX_FREQ);
        std::cout << "Swap Freq: " << swapFreq << std::endl;
        swapFreqSet = true;
    }

    void linearPlacement() {
        const float* fftOut = audio.getFFTPtr();
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
        float mu2 = mu * mu;
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 = y1;

        return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
    }

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
    HoldArray peakRMSHolds;

    SmoothArraySoA gpuFFT;
    HoldArray fftHolds;
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

    float swapFreq = 0.0f;
    bool swapFreqSet = false;
};

