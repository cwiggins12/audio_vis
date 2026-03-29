#pragma once

#include "hold_value.hpp"
#include "smooth_value.hpp"
#include "audio.hpp"
#include "spec.hpp"
#include <algorithm>

//probably want to put these in a globals.h at some point if I wind up with too many
static constexpr float MIN_FREQ = 20.0f;
static constexpr float MAX_FREQ = 20000.0f;
static constexpr float MIN_DB = -96.0f;

class AVBridge {
public:
    AVBridge(Audio& a, Spec& spec) : audio(a), currSpec(spec) {}
    ~AVBridge() {}
    AVBridge(const AVBridge&) = delete;
    AVBridge& operator=(const AVBridge&) = delete;
    AVBridge(AVBridge&&) = delete;
    AVBridge& operator=(AVBridge&&) = delete;

    void init(uint32_t deviceFrameRate, int w, int h) {
        frameRate = deviceFrameRate;
        binAmt = audio.getFFTSize() / 2 + 1;
        channels = audio.getNumChannels();
        sampleRate = audio.getSampleRate();
        initWidth = w;
        initHeight = h;
        currentWidth = w;
        currentHeight = h;
        swap(currSpec);
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

    void resize(int w, int h) {
        currentWidth = w;
        currentHeight = h;
        if (currSpec.customFFTSizeScalesWithWindow == NO_SCALE ||
            currSpec.fftOutputMode != CUSTOM_SIZE) return;

        gpuFFTSize = getSizeFromModeSwitch(w, h);
        setIndexFreqs(gpuFFTSize);

        const bool isFFTdB = currSpec.fftOutputMeasurement == 2;
        float fftMin = isFFTdB ? MIN_DB : 0.0f;
        if (currSpec.useFFTSmoothing) {
            gpuFFT.reset(frameRate, 1.0, currSpec.fftAtk, currSpec.fftRls,
                         gpuFFTSize, fftMin);
        }
        else {
            gpuFFT.reset(frameRate, 0.0f, 0.0f, 0.0f, gpuFFTSize, fftMin);
        }
        if (currSpec.getsFFTHolds) {
            fftHolds.reset(frameRate, currSpec.fftHoldTime, currSpec.fftHoldScalar,
                           isFFTdB, gpuFFTSize);
        }
    }

    void swapSpec(Spec& newSpec) {
        swap(newSpec);
        currSpec = newSpec;
    }

    size_t getFFTGPUSize() {
        return gpuFFTSize;
    }

    size_t getPeakRMSGPUSize() {
        return (currSpec.isPeakRMSMono) ? 2 : channels * 2;
    }

    size_t getFFTGPUSizeInBytes() {
        return getFFTGPUSize() * sizeof(float);
    }

    size_t getPeakRMSGPUSizeInBytes() {
        return getPeakRMSGPUSize() * sizeof(float);
    }

    size_t getValFromHeightScalar(size_t size) {
        return std::round(size * ((float)currentHeight / (float)initHeight));
    }

    size_t getValFromWidthScalar(size_t size) {
        return std::round(size * ((float)currentWidth / (float)initWidth));
    }

    size_t getValFromResolutionScalar(size_t size) {
        return std::round(size *((float)currentHeight * (float)currentWidth)
                            / ((float)initHeight * (float)initWidth));
    }

    size_t getSizeFromModeSwitch(size_t size, int mode) {
        switch (mode) {
            case 0: return size;
            case 1: return getValFromWidthScalar(size);
            case 2: return getValFromHeightScalar(size);
            case 3: return getValFromResolutionScalar(size);
            default: return size;
        }
    }

    void formatData() {
        //peak/rms pop and format
        const int chan = (currSpec.isPeakRMSMono) ? 1 : channels;
        const bool getPRHolds = currSpec.getsPeakRMSHolds;
        const bool isPRdB = currSpec.isPeakRMSdB;
        for (int ch = 0; ch < chan; ++ch) {
            float peak = audio.popPeak(ch);
            float rms = audio.popRMS(ch);
            if (isPRdB) {
                peak = gainToDB(peak);
                rms = gainToDB(rms);
            }
            int pInd = ch * 2;
            int rInd = ch * 2 + 1;
            if (getPRHolds) {
                peakRMSHolds.compareValAtIndex(pInd, peak);
                peakRMSHolds.compareValAtIndex(rInd, rms);
            }
            gpuPeakRMS.setTargetVal(pInd, peak);
            gpuPeakRMS.setTargetVal(rInd, rms);
        }
        //fft output then check holds against that
        switch (currSpec.fftOutputMode) {
            case 0: fullBinPlacement();         break;
            case 1: audibleBinPlacement();      break;
            case 2: customSizeFFTPlacement();   break;
            default: fullBinPlacement();        break;
        }
        if (currSpec.getsFFTHolds) {
            for (int i = 0; i < gpuFFTSize; ++i) {
                fftHolds.compareValAtIndex(i, gpuFFT.getCurrentVal(i));
            }
        }
    }

private:
    //refactor pls
    void swap(Spec& newSpec) {
        //set fftSize, probably just need this in init, but its gonna live here for now
        fftSize = audio.getFFTSize();
        //config peak/RMS hold array
        uint32_t peakRMSSize = (newSpec.isPeakRMSMono) ? 2 : channels * 2;
        if (newSpec.getsPeakRMSHolds) {
            peakRMSHolds.reset(frameRate, newSpec.peakRMSHoldTime,
                               newSpec.peakRMSHoldScalar,
                               newSpec.isPeakRMSdB, peakRMSSize);
        }
        else {
            peakRMSHolds.reset(frameRate, 0.0f, 0.0f, false, 0);
        }
        //config peak/RMS smooth array
        float prMin = newSpec.isPeakRMSdB ? MIN_DB : 0.0f;
        if (newSpec.usePeakRMSSmoothing) {
            gpuPeakRMS.reset(frameRate, 1.0f, newSpec.peakRMSAtk, newSpec.peakRMSAtk,
                             peakRMSSize, prMin);
        }
        else {
            gpuPeakRMS.reset(frameRate, 0.0f, 0.0f, 0.0f, peakRMSSize, prMin);
        }
        //config fft size
        audio.getAudibleRange(&audibleStart, &audibleSize);
        switch (newSpec.fftOutputMode) {
            case 0: {
                gpuFFTSize = binAmt;
                temp.resize(0);
                indexFreqs.resize(0);
                break;
            }
            case 1: {
                gpuFFTSize = audibleSize;
                temp.resize(0);
                indexFreqs.resize(0);
                break;
            }
            case 2: {
                size_t s = newSpec.customFFTSize;
                gpuFFTSize = getSizeFromModeSwitch(s, newSpec.customFFTSizeScalesWithWindow);
                temp.resize(gpuFFTSize);
                setIndexFreqs(gpuFFTSize);
                break;
            }
        }
        const bool isFFTdB = currSpec.fftOutputMeasurement == 2;
        //config FFT holds
        if (newSpec.getsFFTHolds) {
            fftHolds.reset(frameRate, newSpec.fftHoldTime, newSpec.fftHoldScalar,
                           isFFTdB, gpuFFTSize);
        }
        else {
            fftHolds.reset(frameRate, 0.0f, 0.0f, false, 0);
        }
        //config fft smooth array
        float fftMin = isFFTdB ? MIN_DB : 0.0f;
        if (newSpec.useFFTSmoothing) {
            gpuFFT.reset(frameRate, 1.0, newSpec.fftAtk, newSpec.fftRls,
                         gpuFFTSize, fftMin);
        }
        else {
            gpuFFT.reset(frameRate, 0.0f, 0.0f, 0.0f, gpuFFTSize, fftMin);
        }
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below bin collating algo
    void setIndexFreqs(uint32_t size) {
        indexFreqs.resize(size);
        const float scale = (float)fftSize / (float)sampleRate;
        setSwapFreq(scale);
        bool swapIndexFound = false;
        if (size < 2) return;

        for (uint32_t i = 0; i < size; ++i) {
            float norm = (float)(i) / (float)(size - 1);
            float freq = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, norm);
            if (!swapIndexFound && freq > swapFreq) {
                swapIndex = i;
                swapIndexFound = true;
                std::cout << "Swap Index: " << swapIndex << std::endl;
            }
            float binIndexFloat = freq * scale;
            indexFreqs[i] = std::min(std::max(binIndexFloat, 0.0f), (float)binAmt - 1);
        }
    }

    //currently set to start when bin density >= index density
    //may want an int scalar > 1 to allow more customization later
    void setSwapFreq(const float scale) {
        const float binWidth = 1.0f / scale;
        const float logRatio = std::log(MAX_FREQ / MIN_FREQ);
        swapFreq = binWidth * (float)(indexFreqs.size() - 1) / logRatio;
        swapFreq = std::min(std::max(swapFreq, MIN_FREQ), MAX_FREQ);
        std::cout << "Swap Freq: " << swapFreq << std::endl;
    }

    void customSizeFFTPlacement() {
        const float* fftOut = audio.getFFTPtr();
        float* tempPtr = temp.data();
        switchOnInterps(0, swapIndex, fftOut, tempPtr, currSpec.lowMode);
        switchOnCollates(swapIndex, gpuFFTSize, fftOut, tempPtr, currSpec.highMode);
        switchOnMeasurement(gpuFFTSize, tempPtr, currSpec.fftOutputMeasurement);
        gpuFFT.setAllTargetsWithPtr(tempPtr);
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
            case STEFFEN:       getSteffen(start, end, in, out);         break;
            case CATMULL_ROM_3: getCatmullRom3pt(start, end, in, out);   break;
        }
    }

    void switchOnCollates(int start, int end, const float* in,
                          float* out, Collates mode) {
        switch (mode) {
            case RMS:           getRMS(start, end, in, out);             break;
            case PEAK:          getPeak(start, end, in, out);            break;
            case POWER_MEAN:    getPowerWeighted(start, end, in, out);   break;
            case L_NORM:        getLNorm(start, end, in, out);           break;
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
            uint32_t bin1 = (uint32_t)cf;
            float t = cf - bin1;
            uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
            float val = in[bin1] + t * (in[bin2] - in[bin1]);
            out[i] = val;
        }
    }

    // 1. PCHIP
    void getPCHIP(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            uint32_t bin1 = (uint32_t)cf;
            float t = cf - bin1;
            uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
            uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);
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
            uint32_t center = (uint32_t)cf;
            float frac = cf - center;
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -A + 1; k <= A; ++k) {
                int bin = (int)center + k;
                if (bin < 0) bin = 0;
                if (bin >= (int)binAmt) bin = (int)binAmt - 1;
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
            uint32_t center = (uint32_t)cf;
            float frac = cf - center;
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -HALF; k <= HALF; ++k) {
                int bin = (int)center + k;
                if (bin < 0) bin = 0;
                if (bin >= (int)binAmt) bin = (int)binAmt - 1;
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
            uint32_t bin1 = (uint32_t)cf;
            float t = cf - bin1;
            uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
            uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);
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
    // Local cubic that is less prone to oscillation than standard Catmull-Rom
    // Uses 5 points; slope weighting suppresses outliers.
    void getAkima(int start, int end, const float* in, float* out) {
        for(int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            uint32_t bin2 = (uint32_t)cf;   // left bracket
            float t = cf - bin2;
            // Clamp extended neighbourhood
            auto clampBin = [&](int b) -> uint32_t {
                return (uint32_t)std::max(0, std::min((int)binAmt - 1, b));
            };
            float y[5] = {
                in[clampBin((int)bin2 - 2)],
                in[clampBin((int)bin2 - 1)],
                in[clampBin((int)bin2    )],
                in[clampBin((int)bin2 + 1)],
                in[clampBin((int)bin2 + 2)]
            };
            // Finite differences (uniform spacing h=1)
            float m[4];
            for (int k = 0; k < 4; ++k) m[k] = y[k+1] - y[k];
            // Akima weights: |difference of successive slopes|
            auto akimaSlope = [](float m0, float m1, float m2, float m3) -> float {
                float w1 = std::abs(m3 - m2);
                float w2 = std::abs(m1 - m0);
                float denom = w1 + w2;
                if (denom < 1e-10f) return 0.5f * (m1 + m2);  // near-flat: average
                return (w1 * m1 + w2 * m2) / denom;
            };
            float s1 = akimaSlope(m[0], m[1], m[2], m[3]);   // slope at bin2
            float s2 = akimaSlope(m[1], m[2], m[3],           // slope at bin2+1
                                  // need m[4]; mirror last delta
                                  m[3] + (m[3] - m[2]));
            // Hermite interpolation
            float t2 = t*t, t3 = t2*t;
            float val = ( 2*t3 - 3*t2 + 1) * y[2]
                        + (   t3 - 2*t2 + t) * s1
                        + (-2*t3 + 3*t2    ) * y[3]
                        + (   t3 -   t2    ) * s2;
            out[i] = val;
        }
    }

    // 6. STEFFEN
    // Monotonic cubic (Steffen 1990). Same no-overshoot guarantee as PCHIP with a
    // simpler slope formula: clamp the harmonic mean of adjacent secants to not
    // exceed 3x either secant. Slightly cheaper than PCHIP.
    void getSteffen(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            float cf = indexFreqs[i];
            uint32_t bin1 = (uint32_t)cf;
            float t = cf - bin1;
            uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
            uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);
            float y0 = in[bin0], y1 = in[bin1];
            float y2 = in[bin2], y3 = in[bin3];
            float d0 = y1 - y0, d1 = y2 - y1, d2 = y3 - y2;
            // Steffen slope: sign-aware harmonic mean, clamped to 3*min(|secants|)
            auto steffenSlope = [](float dm, float dp) -> float {
                if (dm * dp <= 0.0f) return 0.0f;
                float p = (dm + dp) * 0.5f;                         // average secant
                float cap = 3.0f * std::min(std::abs(dm), std::abs(dp));
                // Preserve sign of p, clamp magnitude
                return (std::abs(p) <= cap) ? p
                     : std::copysign(cap, p);
            };
            float m1 = steffenSlope(d0, d1);
            float m2 = steffenSlope(d1, d2);
            float t2 = t * t, t3 = t2 * t;
            float h00 =  2*t3 - 3*t2 + 1;
            float h10 =    t3 - 2*t2 + t;
            float h01 = -2*t3 + 3*t2;
            float h11 =    t3 -   t2;
            float val = h00*y1 + h10*m1 + h01*y2 + h11*m2;
            out[i] = val;
        }
    }

    // 7. CATMULL_ROM_3
    void getCatmullRom3pt(int start, int end, const float* in, float* out) {
        for (int i = start; i < end; ++i) {
            float centerBinFloat = indexFreqs[i];
            uint32_t bin1 = (int)centerBinFloat;
            float mu = centerBinFloat - bin1;
            uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
            uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
            uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);
            float y0 = in[bin0];
            float y1 = in[bin1];
            float y2 = in[bin2];
            float y3 = in[bin3];
            float mu2 = mu * mu;
            float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float a2 = -0.5f * y0 + 0.5f * y2;
            float a3 = y1;
            float val = a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
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
                out[i] = gainToDB(in[highB]);
                continue;
            }
            float weightedSum = 0.0f;
            float weightSum   = 0.0f;
            for (int j = lowB; j <= highB; ++j) {
                float gain  = dBToGain(in[j]);
                float power = gain * gain;
                weightedSum += power * in[j];
                weightSum   += power;
            }
            float val = (weightSum > 1e-30f) ? weightedSum / weightSum : MIN_DB;
            out[i] = gainToDB(val);
        }
    }

    // 3. L-NORM
    void getLNorm(int start, int end, const float* in, float* out, float p = 2.0f) {
        for (int i = start; i < end; ++i) {
            int lowB  = (i > 0) ? (int)indexFreqs[i - 1] + 1 : 0;
            int highB = (int)indexFreqs[i];
            int count = highB - lowB + 1;
            if (count == 1) {
                out[i] = gainToDB(in[highB]);
                continue;
            }
            float sum = 0.0f;
            for (int j = lowB; j <= highB; ++j) {
                float gain = dBToGain(in[j]);
                sum += std::pow(gain, p);
            }
            float val = std::pow(sum / (float)count, 1.0f / p);
            out[i] = gainToDB(val);
        }
    }

    void audibleBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        for (uint32_t i = 0; i < audibleSize; ++i) {
            gpuFFT.setTargetVal(i, fftPtr[i + audibleStart]);
        }
    }

    void fullBinPlacement() {
        const float* fftPtr = audio.getFFTPtr();
        for (uint32_t i = 0; i < binAmt; ++i) {
            gpuFFT.setTargetVal(i, fftPtr[i]);
        }
    }

    //float to float gain/mag to dB helper
    float gainToDB(float gain) {
        return std::max(MIN_DB, 20.0f * std::log10(gain));
    }

    //float to float db to gain/mag helper
    float dBToGain(float dB) {
        dB = std::max(MIN_DB, dB);
        return std::pow(10.0f, dB * 0.05f);
    }

    //float to float db to power helper
    float dBToPower(float dB) {
        dB = std::max(MIN_DB, dB);
        return std::pow(10.0f, dB * 0.1f);
    }

    Audio& audio;
    Spec currSpec;

    SmoothArraySoA gpuPeakRMS;
    HoldArray peakRMSHolds;

    SmoothArraySoA gpuFFT;
    HoldArray fftHolds;
    std::vector<float> indexFreqs;
    //truly thought I could go without a single temp array
    //was hoping for ring buffer, to analysis objs, straight to smooth
    //but adding this made everything easier for me and, hopefully, the compiler
    //pray for vectorizing
    std::vector<float> temp;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;
    uint32_t hopAmt = 0;
    uint32_t binAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t frameRate = 0;

    uint32_t audibleStart = 0;
    uint32_t audibleSize = 0;

    uint32_t baseFFTSize = 0;
    uint32_t gpuFFTSize = 0;
    uint32_t swapIndex = 0;

    int initWidth = 0;
    int initHeight = 0;
    int currentWidth = 0;
    int currentHeight = 0;

    float swapFreq = 0.0f;
};

