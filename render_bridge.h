#pragma once

#include "smooth_value.h"
#include "audio.h"
#include <cstdint>

//for right now the fft_size is static, if I see any demand for a change I'll debate it
//it would be really expensive for very little gain that I know of
struct AudioSpec {
    //TODO: these all need comments to explain how they can affect the data and why
    uint32_t hopAmt;
    uint32_t smoothSize;

    bool getsPeakHolds;
    bool isPeakMono;
    bool isRMSMono;
    bool getsFFTHolds;

    bool isPerceptual;
    bool isHannWindowed;
    bool isDB;
    bool isSingleSided;

    float slope;
};

class AVBridge {
    AVBridge(Audio& a, AudioSpec& spec) : audio(a), currSpec(spec) {}
    AVBridge(Audio& a, uint32_t hopAmt, uint32_t smoothSize, bool peakHolds, 
             bool peakMono, bool rmsMono, bool fftHolds, bool isPerceptual, 
             bool isHannWindowed, bool isDB, bool isSingleSided, float slope)
             : audio(a) {
        currSpec = {hopAmt, smoothSize, peakHolds, peakMono, rmsMono, fftHolds,
                    isPerceptual, isHannWindowed, isDB, isSingleSided, slope};
    }
    ~AVBridge() {}
    AVBridge(const AVBridge&) = delete;
    AVBridge& operator=(const AVBridge&) = delete;
    AVBridge(AVBridge&&) = delete;
    AVBridge& operator=(AVBridge&&) = delete;

    void init() {
        
    }

    void nextFrame() {
        rmsPeakPerChannel.advanceAllAsym();
        smoothFFT.advanceAllAsym();
    }

    const float* getSmoothFFTPtr() {
        return smoothFFT.getCurrents();
    }

    const float* getRMSPeakPtr() {
        return rmsPeakPerChannel.getCurrents();
    }

    void resize(size_t newSize) {
        setSmoothIndexFreqs(newSize);
        smoothFFTSize = newSize;
        smoothFFT.resize(newSize);
        //either separate call to Audio or call it here
        //firstWindowAccumulated = false;
    }

    void swapSpec(AudioSpec& newSpec) {
        
    }

    void formatData() {

    }

private:
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
        int bin1 = (int)centerBinFloat;
        float fraction = centerBinFloat - bin1; 

        int bin0 = std::max(0, bin1 - 1);
        int bin2 = std::min(binAmt - 1, bin1 + 1);
        int bin3 = std::min(binAmt, bin1 + 2);

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
        for (int i = 0; i < audibleSize; ++i) {
            smooth[i] = fftPtr[i + firstAudibleIndex];
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

    SmoothArraySoA rmsPeakPerChannel;
    SmoothArraySoA smoothFFT;
    std::vector<float> smoothIndexFreqs;

    //const uint32_t fftOrder;

    bool firstWindowAccumulated = false;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;
    uint32_t hopAmt = 0;
    int binAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    uint32_t firstAudibleIndex = 0;
    uint32_t audibleSize = 0;
    size_t smoothFFTSize;
    uint32_t swapIndex = 0;
};

