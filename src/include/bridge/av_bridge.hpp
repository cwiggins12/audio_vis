#pragma once

#include "bridge/hold_value.hpp"
#include "bridge/smooth_value.hpp"
#include "audio/audio.hpp"
#include "config/spec.hpp"
#include <algorithm>

class AVBridge {
public:
    AVBridge(Audio& a, Spec& spec, Globals& g) : audio(a), globals(g) {
        currSpec = &spec;
    }
    ~AVBridge() {}
    AVBridge(const AVBridge&) = delete;
    AVBridge& operator=(const AVBridge&) = delete;
    AVBridge(AVBridge&&) = delete;
    AVBridge& operator=(AVBridge&&) = delete;

    void init() {
        swapPeakRMS();
        swapSizeChanges();
        swapFFT();
    }

    void nextFrame() {
        gpuFFT.advanceAll();
        gpuPeakRMS.advanceAll();
        fftHolds.countdownAll(gpuFFT.getCurrents());
        peakRMSHolds.countdownAll(gpuPeakRMS.getCurrents());
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

    const float* getRawSamplePtr() {
        return audio.getSamplePtr();
    }

    void swapSpec(Spec& newSpec) {
        currSpec = &newSpec;
        swapPeakRMS();
        swapSizeChanges();
        swapFFT();
    }

    size_t getPeakRMSGPUSize() {
        return (currSpec->isPeakRMSMono) ? 2 : globals.numChannels * 2;
    }

    size_t getPeakRMSGPUSizeInBytes() {
        return getPeakRMSGPUSize() * sizeof(float);
    }

    size_t getRawSampleSizeInBytes() {
        return (currSpec->isRawSamplesMono) ? globals.hopSize * sizeof(float) :
                                  globals.hopSize * globals.numChannels * sizeof(float);
    }

    void formatData() {
        //peak/rms
        const int size = (currSpec->isPeakRMSMono) ? 2 : globals.numChannels * 2;
        float* prPtr = audio.getPRPtr();
        if (currSpec->isPeakRMSdB) {
            for (int i = 0; i < size; ++i) {
                prPtr[i] = gainToDB(prPtr[i]);
            }
        }
        if (currSpec->getPeakRMSHolds) {
            peakRMSHolds.compareValsToArray(prPtr);
        }
        if (currSpec->isRawSamplesdB) {
            float* rawSampPtr = audio.getSamplePtr();
            int rawSampSize = (currSpec->isRawSamplesMono) ? globals.hopSize :
                                                  globals.hopSize * globals.numChannels;
            for (int i = 0; i < rawSampSize; ++i) {
                rawSampPtr[i] = gainToDB(rawSampPtr[i]);
            }
        }
        gpuPeakRMS.setAllTargetsWithPtr(prPtr);
        audio.prClear();
        //fft
        switch (currSpec->fftOutputMode) {
            case 0: fullBinPlacement();         break;
            case 1: audibleBinPlacement();      break;
            case 2: customSizeFFTPlacement();   break;
            default: fullBinPlacement();        break;
        }
        if (currSpec->getFFTHolds) {
            fftHolds.compareValsToArray(gpuFFT.getCurrents());
        }
    }

private:
    void swapPeakRMS() {
        //config peak/RMS hold array
        uint32_t peakRMSSize = (currSpec->isPeakRMSMono) ? 2 : globals.numChannels * 2;
        if (currSpec->getPeakRMSHolds) {
            peakRMSHolds.reset(globals.displayHz, currSpec->peakRMSHoldTime,
                               currSpec->peakRMSHoldScalar,
                               currSpec->isPeakRMSdB, peakRMSSize);
        }
        else {
            peakRMSHolds.reset(globals.displayHz, 0.0f, 0.0f, false, 0);
        }
        //config peak/RMS smooth array
        float prMin = currSpec->isPeakRMSdB ? MIN_DB : 0.0f;
        if (currSpec->usePeakRMSSmoothing) {
            gpuPeakRMS.reset(globals.displayHz, 1.0f, currSpec->peakRMSAtk,
                             currSpec->peakRMSRls, peakRMSSize, prMin);
        }
        else {
            gpuPeakRMS.reset(globals.displayHz, 0.0f, 0.0f, 0.0f, peakRMSSize, prMin);
        }
    }

    void swapSizeChanges() {
        switch (currSpec->fftOutputMode) {
            case 0: {
                globals.fftArrSize = globals.fftBinAmt;
                middlemanBuffer.resize(0);
                indexFreqs.resize(0);
                break;
            }
            case 1: {
                audio.getAudibleRange(&audibleStart, &audibleSize);
                globals.fftArrSize = audibleSize;
                middlemanBuffer.resize(0);
                indexFreqs.resize(0);
                break;
            }
            case 2: {
                size_t s = currSpec->customFFTSize;
                globals.fftArrSize = globals.getSizeFromModeSwitch(s,
                                            currSpec->customFFTSizeScalesWithWindow);
                middlemanBuffer.resize(globals.fftArrSize);
                setIndexFreqs(globals.fftArrSize);
                break;
            }
        }
    }

    void swapFFT() {
        const bool isFFTdB = currSpec->fftOutputMeasurement == DECIBELS;
        if (currSpec->getFFTHolds) {
            fftHolds.reset(globals.displayHz, currSpec->fftHoldTime,
                           currSpec->fftHoldScalar, isFFTdB, globals.fftArrSize);
        }
        else {
            fftHolds.reset(globals.displayHz, 0.0f, 0.0f, false, 0);
        }
        //config fft smooth array
        float fftMin = isFFTdB ? MIN_DB : 0.0f;
        if (currSpec->useFFTSmoothing) {
            gpuFFT.reset(globals.displayHz, 1.0, currSpec->fftAtk, currSpec->fftRls,
                         globals.fftArrSize, fftMin);
        }
        else {
            gpuFFT.reset(globals.displayHz, 0.0f, 0.0f, 0.0f,
                         globals.fftArrSize, fftMin);
        }
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below bin collating algo
    void setIndexFreqs(int size) {
        indexFreqs.resize(size);
        const float scale = (float)globals.fftSize / (float)globals.sampleRate;
        setSwapFreq(scale);
        bool swapIndexFound = false;
        if (size < 2) return;

        for (int i = 0; i < size; ++i) {
            float norm = (float)(i) / (float)(size - 1);
            float freq = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, norm);
            if (!swapIndexFound && freq > swapFreq) {
                swapIndex = i;
                swapIndexFound = true;
            }
            float binIndexFloat = freq * scale;
            indexFreqs[i] = std::min(std::max(binIndexFloat, 0.0f),
                                             (float)globals.fftBinAmt - 1);
        }
    }

    //set to swap when bin density >= index density
    void setSwapFreq(const float scale) {
        const float binWidth = 1.0f / scale;
        const float logRatio = std::log(MAX_FREQ / MIN_FREQ);
        swapFreq = binWidth * (float)(indexFreqs.size() - 1) / logRatio;
        swapFreq = std::min(std::max(swapFreq, MIN_FREQ), MAX_FREQ);
    }

    void customSizeFFTPlacement() {
        const float* fftOut = audio.getFFTPtr();
        float* buffPtr = middlemanBuffer.data();
        const float* srcBins = fftOut;
        if (currSpec->highSmoothing > 0.0f) {
            int swapBin = (swapIndex > 0 && swapIndex < (int)indexFreqs.size())
                        ? (int)indexFreqs[swapIndex] : 0;
            gaussianBinPass(fftOut, globals.fftBinAmt, swapBin,
                            currSpec->highSmoothing);
            srcBins = gaussScratch.data();
        }
        switchOnInterps(0, swapIndex, srcBins, buffPtr, currSpec->lowMode);
        switchOnCollates(swapIndex, globals.fftArrSize, srcBins,
                         buffPtr, currSpec->highMode);
        switchOnMeasurement(globals.fftArrSize,
                            buffPtr, currSpec->fftOutputMeasurement);
        gpuFFT.setAllTargetsWithPtr(buffPtr);
    }

    void switchOnInterps(int start, int end, const float* in,
                         float* out, Interps mode) {
        switch (mode) {
            case LINEAR:        getLinear(start, end, in, out);          break;
            case PCHIP:         getPCHIP(start, end, in, out);           break;
            case LANCZOS:       getLanczos(start, end, in, out);         break;
            case GAUSSIAN:      getGaussian(start, end, in, out);        break;
            case CUBIC_B:       getBSpline(start, end, in, out);         break;
            case AKIMA:         getAkima(start, end, in, out);           break;
        }
    }

    void switchOnCollates(int start, int end, const float* in,
                          float* out, Collates mode) {
        switch (mode) {
            case RMS:           getRMS(start, end, in, out);             break;
            case PEAK:          getPeak(start, end, in, out);            break;
            case POWER_MEAN:    getPowerWeighted(start, end, in, out);   break;
        }
    }

    void switchOnMeasurement(int size, float* arr, FFTMeasurement mode) {
        switch (mode) {
            case POWER:         dBToPowerArray(size, arr);              break;
            case MAGNITUDE:     dBToMagArray(size, arr);                break;
            case DECIBELS:                                              break;
        }
    }

    void dBToMagArray(int size, float* arr) {
        for (int i = 0; i < size; ++i) {
            arr[i] = dBToGain(arr[i]);
        }
    }

    void dBToPowerArray(int size, float* arr) {
        for (int i = 0; i < size; ++i) {
            arr[i] = dBToPower(arr[i]);
        }
    }

    // Low-end interpolation strats
    // 0. LINEAR
    void getLinear(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            int bin1 = cf;
            float t = cf - bin1;
            int bin2 = std::min(globals.fftBinAmt - 1, bin1 + 1);
            float val = in[bin1] + t * (in[bin2] - in[bin1]);
            out[i] = val;
        }
    }

    // 1. PCHIP
    void getPCHIP(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            int bin1 = cf;
            float t = cf - bin1;
            int bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            int bin2 = std::min(globals.fftBinAmt - 1, bin1 + 1);
            int bin3 = std::min(globals.fftBinAmt - 1, bin1 + 2);
            float y0 = in[bin0], y1 = in[bin1];
            float y2 = in[bin2], y3 = in[bin3];
            // Secants
            float d0 = y1 - y0;   // h=1 for all, so secant == delta
            float d1 = y2 - y1;
            float d2 = y3 - y2;
            // Fritsch-Carlson slopes
            auto pchipSlope = [](float dm, float dp) -> float {
                if (dm * dp <= 0.0f) return 0.0f;          // local extremum – flatten
                float w1 = 2.0f * dp + dm, w2 = dp + 2.0f * dm;
                return (w1 + w2) / (w1 / dm + w2 / dp);    // harmonic mean weights
            };
            float m1 = pchipSlope(d0, d1);   // slope at bin1
            float m2 = pchipSlope(d1, d2);   // slope at bin2
            // Hermite basis
            float t2 = t * t, t3 = t2 * t;
            float h00 =  2*t3 - 3*t2 + 1;
            float h10 =    t3 - 2*t2 + t;
            float h01 = -2*t3 + 3*t2;
            float h11 =    t3 -   t2;
            float val = h00*y1 + h10*m1 + h01*y2 + h11*m2;
            out[i] = val;
        }
    }

    // 2. LANCZOS
    void getLanczos(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            constexpr int A = 4;    // lobe count; 2–4 typical; higher = sharper/slower
            auto lanczosKernel = [](float x) -> float {
                if (std::abs(x) < 1e-6f)  return 1.0f;
                if (std::abs(x) >= (float)A) return 0.0f;
                float px = M_PI * x;
                return (float)A * std::sin(px) * std::sin(px / (float)A) / (px * px);
            };
            float cf = indexFreqs[i];
            int center = cf;
            float frac = cf - center;
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -A + 1; k <= A; ++k) {
                int bin = center + k;
                if (bin < 0) bin = 0;
                if (bin >= globals.fftBinAmt) bin = globals.fftBinAmt - 1;
                float w = lanczosKernel((float)k - frac);
                sum  += w * in[bin];
                wsum += w;
            }
            float val = (wsum > 1e-9f) ? sum / wsum : in[center];
            out[i] = val;
        }
    }

    // 3. GAUSSIAN
    void getGaussian(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            constexpr float SIGMA = 1.0f;   // std-dev in bins; increase for more blur
            constexpr int   HALF  = 3;      // half-window (3*sigma covers 99.7 %)
            float cf = indexFreqs[i];
            int center = cf;
            float frac = cf - center;
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -HALF; k <= HALF; ++k) {
                int bin = center + k;
                if (bin < 0) bin = 0;
                if (bin >= globals.fftBinAmt) bin = globals.fftBinAmt - 1;
                float dist = (float)k - frac;
                float w    = std::exp(-0.5f * dist * dist / (SIGMA * SIGMA));
                sum  += w * in[bin];
                wsum += w;
            }
            float val = (wsum > 1e-9f) ? sum / wsum : in[center];
            out[i] = val;
        }
    }

    // 4. CUBIC_B
    void getBSpline(int start, int end, const float* in, float* out) {
        for(int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            int bin1 = cf;
            float t = cf - bin1;
            int bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            int bin2 = std::min(globals.fftBinAmt - 1, bin1 + 1);
            int bin3 = std::min(globals.fftBinAmt - 1, bin1 + 2);
            float y0 = in[bin0], y1 = in[bin1];
            float y2 = in[bin2], y3 = in[bin3];
            float t2 = t * t, t3 = t2 * t;
            float b0 = (1.0f - 3*t + 3*t2 -   t3) / 6.0f;
            float b1 = (4.0f         - 6*t2 + 3*t3) / 6.0f;
            float b2 = (1.0f + 3*t + 3*t2 - 3*t3) / 6.0f;
            float b3 =                          t3  / 6.0f;
            float val = b0*y0 + b1*y1 + b2*y2 + b3*y3;
            out[i] = val;
        }
    }

    // 5. AKIMA
    void getAkima(int start, int end, const float* in, float* out) {
        for(int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            int bin2 = cf;   // left bracket
            float t = cf - bin2;
            auto clampBin = [&](int b) -> int {
                return std::max(0, std::min(globals.fftBinAmt - 1, b));
            };
            float y[5] = {
                in[clampBin(bin2 - 2)],
                in[clampBin(bin2 - 1)],
                in[clampBin(bin2    )],
                in[clampBin(bin2 + 1)],
                in[clampBin(bin2 + 2)]
            };
            // Finite differences (uniform spacing h=1)
            float m[4];
            for (int k = 0; k < 4; ++k) m[k] = y[k+1] - y[k];
            // Akima weights: difference of successive slopes
            auto akimaSlope = [](float m0, float m1, float m2, float m3) -> float {
                float w1 = std::abs(m3 - m2);
                float w2 = std::abs(m1 - m0);
                float denom = w1 + w2;
                if (denom < 1e-10f) return 0.5f * (m1 + m2);  // near-flat: average
                return (w1 * m1 + w2 * m2) / denom;
            };
            float s1 = akimaSlope(m[0], m[1], m[2], m[3]);
            float s2 = akimaSlope(m[1], m[2], m[3], m[3] + (m[3] - m[2]));
            float t2 = t*t, t3 = t2*t;
            float val = ( 2*t3 - 3*t2 + 1) * y[2]
                        + (   t3 - 2*t2 + t) * s1
                        + (-2*t3 + 3*t2    ) * y[3]
                        + (   t3 -   t2    ) * s2;
            out[i] = val;
        }
    }

    // High-end bin collation strats
    // 0. RMS
    void getRMS(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            int lowB = (i > 0) ? (int)indexFreqs[i - 1] + 1 : 0;
            int highB = (int)indexFreqs[i];
            float sumSq = 0.0f;
            for (int j = lowB; j <= highB; ++j) {
                float mag = dBToGain(in[j]);
                sumSq += mag * mag;
            }
            float rms = std::sqrt(sumSq / (highB - lowB + 1));
            out[i] = gainToDB(rms);
        }
    }

    // 1. PEAK
    void getPeak(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            int lowB = (i > 0) ? (int)indexFreqs[i - 1] + 1 : 0;
            int highB = (int)indexFreqs[i];
            float peak = MIN_DB;
            for (int j = lowB; j <= highB; ++j) {
                peak = std::max(in[j], peak);
            }
            out[i] = peak;
        }
    }

    // 2. POWER-WEIGHTED MEAN
    void getPowerWeighted(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            int lowB  = (i > 0) ? (int)indexFreqs[i - 1] + 1 : 0;
            int highB = (int)indexFreqs[i];
            int count = highB - lowB + 1;
            if (count == 1) {
                out[i] = in[highB];
                continue;
            }
            float weightedSum = 0.0f;
            float weightSum   = 0.0f;
            for (int j = lowB; j <= highB; ++j) {
                float gain  = dBToGain(in[j]);
                float power = gain * gain;
                weightedSum += power * gain;
                weightSum   += power;
            }
            float val = (weightSum > 1e-30f) ? weightedSum / weightSum : MIN_DB;
            out[i] = gainToDB(val);
        }
    }

    void gaussianBinPass(const float* fftOut, int binCount, int swapBin,
                         float maxSigma) {
        gaussScratch.resize(binCount); 
        // Copy low end through unchanged
        for (int i = 0; i < swapBin && i < binCount; ++i) {
            gaussScratch[i] = fftOut[i];
        }
        if (swapBin >= binCount) return;
        float startSigma = maxSigma * ((float)swapBin / (float)binCount);
        float sigmaRange = maxSigma - startSigma;
        // Log growth from swapBin to end
        float logRange = std::log((float)(binCount - swapBin));
        for (int i = swapBin; i < binCount; ++i) {
            float logPos = std::log((float)(i - swapBin + 1));
            float sigma  = startSigma + sigmaRange * (logPos / logRange);
            if (sigma < 0.5f) {
                gaussScratch[i] = fftOut[i];
                continue;
            }
            int halfW = std::min((int)(3.0f * sigma + 0.5f), 24);
            float sum  = 0.0f;
            float wsum = 0.0f;
            float sig2 = sigma * sigma;
            for (int k = -halfW; k <= halfW; ++k) {
                int idx = i + k;
                if (idx < 0 || idx >= binCount) continue;
                float w = std::exp(-0.5f * (float)(k * k) / sig2);
                sum  += w * fftOut[idx];
                wsum += w;
            }
            gaussScratch[i] = (wsum > 1e-9f) ? sum / wsum : fftOut[i];
        }
    }

    void audibleBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        for (int i = 0; i < audibleSize; ++i) {
            gpuFFT.setTargetVal(i, fftPtr[i + audibleStart]);
        }
    }

    void fullBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        for (int i = 0; i < globals.fftBinAmt; ++i) {
            gpuFFT.setTargetVal(i, fftPtr[i]);
        }
    }

    float gainToDB(float gain) {
        return std::max(MIN_DB, 20.0f * std::log10(gain));
    }

    float dBToGain(float dB) {
        dB = std::max(MIN_DB, dB);
        return std::pow(10.0f, dB * 0.05f);
    }

    float dBToPower(float dB) {
        dB = std::max(MIN_DB, dB);
        return std::pow(10.0f, dB * 0.1f);
    }

    Audio& audio;
    Spec* currSpec = nullptr;
    Globals& globals;
    SmoothArraySoA gpuPeakRMS;
    HoldArray peakRMSHolds;
    SmoothArraySoA gpuFFT;
    HoldArray fftHolds;
    std::vector<float> indexFreqs;
    std::vector<float> middlemanBuffer;
    std::vector<float> gaussScratch;
    int audibleStart = 0;
    int audibleSize = 0;
    int swapIndex = 0;
    float swapFreq = 0.0f;
};

