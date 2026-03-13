#pragma once

#include "float_holds.h"
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
        fftHolds.resize(0, MIN_DB);
        peakRMSHolds.resize(0, MIN_DB);
        swap(currSpec);
    }

    void nextFrame() {
        //TODO: would adding if/else here for calls where smoothing is disabled call next over asym
        gpuPeakRMS.advanceAllAsym();
        gpuFFT.advanceAllAsym();
        fftHolds.countdownAll();
        peakRMSHolds.countdownAll();
    }

    const float* getGPUFFTPtr() {
        return gpuFFT.getCurrents();
    }

    const float* getPeakRMSPtr() {
        return gpuPeakRMS.getCurrents();
    }

    void resize(int w, int h) {
        //NOTE: this could be a race on different threading(if this becomes a callback, etc.)
        //keep in line in sdl event loop
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
                fftHolds.resize(w, minVal);
            }
            gpuPeakRMS.setAllCurrentAndTargets(0.0f);
            peakRMSHolds.clear(MIN_DB);
        }
        else if (currSpec.isSizeHeightDependent) {
            h *= heightScalar;
            setIndexFreqs(h);
            gpuFFTSize = h;
            gpuFFT.resize(h);
            if (currSpec.getsFFTHolds) {
                float minVal = (currSpec.isFFTdB) ? MIN_DB : 0.0f;
                fftHolds.resize(h, minVal);
            }
            gpuPeakRMS.setAllCurrentAndTargets(0.0f);
            peakRMSHolds.clear(MIN_DB);
        }
    }

    void swapSpec(AudioSpec& newSpec) {
        swap(newSpec);
        currSpec = newSpec;
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
        //I know this looks pretty nasty, but its the best I could get it to look at and its clear.
        //The logic is just nasty to make this work
        const bool peakMono = currSpec.isPeakMono;
        const bool rmsMono = currSpec.isRMSMono;
        const bool getPRHolds = currSpec.getsPeakRMSHolds;

        float peak = popPeak(0);
        gpuPeakRMS.setTargetVal(0, peak);
        float rms = popRMS(0);
        gpuPeakRMS.setTargetVal(1, popRMS(0));
        if (getPRHolds) {
            peakRMSHolds.compareValAtIndex(0, peak);
            peakRMSHolds.compareValAtIndex(1, rms);
        }

        if (!peakMono) {
            const int offset = 2;
            const int stride = rmsMono ? 1 : 2;
            for (int ch = 1; ch < channels; ++ch) {
                peak = popPeak(ch);
                gpuPeakRMS.setTargetVal(offset + (ch - 1) * stride, peak);
                if (getPRHolds) {
                    peakRMSHolds.compareValAtIndex(ch, peak);
                }
            }
        }

        if (!rmsMono) {
            const int offset = peakMono ? 2 : 3;
            const int stride = peakMono ? 1 : 2;
            for (int ch = 1; ch < channels; ++ch) {
                rms = popRMS(ch);
                gpuPeakRMS.setTargetVal(offset + (ch - 1) * stride, rms);
                if (getPRHolds) {
                    peakRMSHolds.compareValAtIndex(ch, rms);
                }
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
        for (int i = 0; i < fftGPUSize; ++i) {
            fftHolds.compareValAtIndex(i, gpuFFT.getTargetVal(i));
        }
    }

private:
    void swap(AudioSpec& newSpec) {
        uint32_t peakRMSSize = 0;
        peakRMSSize += (newSpec.isPeakMono) ? 1 : channels;
        peakRMSSize += (newSpec.isRMSMono) ? 1 : channels;

        if (newSpec.getsPeakRMSHolds) {
            peakRMSHolds.reset(frameRate, newSpec.peakRMSHoldTime);
            peakRMSHolds.resize(peakRMSSize, MIN_DB);
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
            fftGPUSize = binAmt;
        }
        else if (newSpec.arbitrarySize == 0 && newSpec.useAudibleSize) {
            fftGPUSize = audibleSize;
        }
        else {
            const bool h = newSpec.isSizeHeightDependent && currentHeight != INIT_HEIGHT;
            const bool w = newSpec.isSizeWidthDependent && currentWidth != INIT_WIDTH;
            if (h && w) {
                //TODO: figure out this too pls.....
            }
            else if (h) {
                fftGPUSize = newSpec.arbitrarySize * heightScalar;
            }
            else if (w) {
                fftGPUSize = newSpec.arbitrarySize * widthScalar;
            }
            else {
                fftGPUSize = newSpec.arbitrarySize;
            }
            setIndexFreqs(fftGPUSize);
        }
        gpuFFT.resize(fftGPUSize);

        if (newSpec.getsFFTHolds) {
            fftHolds.reset(frameRate, newSpec.fftHoldTime);
            fftHolds.resize(fftGPUSize, MIN_DB);
        }
        if (newSpec.useFFTSmoothing) {
            gpuFFT.reset(0);
            gpuFFT.setAsym(1,1);
        }
        else {
            gpuFFT.reset(frameRate);
            gpuFFT.setAsym(newSpec.fftAtk, newSpec.fftRls);
        }

        if (newSpec.usePeakRMSSmoothing) {
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
        float swapFreq = 0.0f; //needs math to find swapFreq
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
        uint32_t bin3 = std::min(binAmt, bin1 + 2);

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

    //pixel gpuing low end helper to cubic interpolate
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

    float popPeak(int ch) {
        float p = audio.popPeak(ch);
        if (currSpec.isPeakdB) {
            p = gainToDB(p, MIN_DB);
        }
        return p;
    }

    float popRMS(int ch) {
        float r = audio.popRMS(ch);
        if (currSpec.isRMSdB) {
            r = gainToDB(r, MIN_DB);
        }
        return r;
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
    std::vector<float> indexFreqs;
    HoldArray fftHolds;
    HoldArray peakRMSHolds;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;
    uint32_t hopAmt = 0;
    uint32_t binAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t frameRate = 0;

    uint32_t audibleStart = 0;
    uint32_t audibleSize = 0;

    uint32_t gpuFFTSize;
    uint32_t swapIndex = 0;

    uint32_t fftGPUSize = 0;

    int currentWidth = 0;
    int currentHeight = 0;
    float widthScalar = 0.0f;
    float heightScalar = 0.0f;
};

