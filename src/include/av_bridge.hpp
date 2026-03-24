#pragma once

#include "hold_value.hpp"
#include "smooth_value.hpp"
#include "audio.hpp"
#include "audio_spec.hpp"
#include <algorithm>

//probably want to put these in a globals.h at some point if I wind up with too many
static constexpr float MIN_FREQ = 20.0f;
static constexpr float MAX_FREQ = 20000.0f;
static constexpr float MIN_DB = -96.0f;

class AVBridge {
public:
    AVBridge(Audio& a, AudioSpec& spec) : audio(a), currSpec(spec) {}
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
        if (!currSpec.isSizeWidthDependent && !currSpec.isSizeHeightDependent) return;

        gpuFFTSize = computeFFTSize(w, h);
        setIndexFreqs(gpuFFTSize);

        float fftMin = currSpec.isFFTdB ? MIN_DB : 0.0f;
        if (currSpec.useFFTSmoothing) {
            gpuFFT.reset(frameRate, currSpec.fftAtk, currSpec.fftRls,
                         gpuFFTSize, fftMin);
        }
        else {
            gpuFFT.reset(frameRate, 0.0f, 0.0f, 0.0f, gpuFFTSize, fftMin);
        }

        if (currSpec.getsFFTHolds) {
            fftHolds.reset(frameRate, currSpec.fftHoldTime, currSpec.fftHoldScalar,
                           currSpec.isFFTdB, gpuFFTSize);
        }
    }

    void swapSpec(AudioSpec& newSpec) {
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
        else if (currSpec.customLogSize == 0) {
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
        else {
            peakRMSHolds.reset(frameRate, 0.0f, 0.0f, false, 0);
        }
        //config peak/RMS smooth array
        float prMin = newSpec.isPeakRMSdB ? MIN_DB : 0.0f;
        if (newSpec.usePeakRMSSmoothing) {
            gpuPeakRMS.reset(frameRate, newSpec.peakRMSAtk, newSpec.peakRMSAtk, peakRMSSize, prMin);
        }
        else {
            gpuPeakRMS.reset(frameRate, 0.0f, 0.0f, 0.0f, peakRMSSize, prMin);
        }
        //config fft size
        audio.getAudibleRange(&audibleStart, &audibleSize);
        if (newSpec.customLogSize == 0 && !newSpec.useAudibleSize) {
            gpuFFTSize = binAmt;
        }
        else if (newSpec.customLogSize == 0 && newSpec.useAudibleSize) {
            gpuFFTSize = audibleSize;
        }
        else {
            baseFFTSize = newSpec.customLogSize;
            const bool h = newSpec.isSizeHeightDependent;
            const bool w = newSpec.isSizeWidthDependent;
            if (h || w) {
                gpuFFTSize = computeFFTSize(currentWidth, currentHeight);
            }
            else {
                gpuFFTSize = baseFFTSize;
            }
            setIndexFreqs(gpuFFTSize);
        }
        //config FFT holds
        if (newSpec.getsFFTHolds) {
            fftHolds.reset(frameRate, newSpec.fftHoldTime, newSpec.fftHoldScalar, 
                               newSpec.isFFTdB, gpuFFTSize);
        }
        else {
            fftHolds.reset(frameRate, 0.0f, 0.0f, false, 0);
        }
        //config fft smooth array
        float fftMin = newSpec.isFFTdB ? MIN_DB : 0.0f;
        if (newSpec.useFFTSmoothing) {
            gpuFFT.reset(frameRate, newSpec.fftAtk, newSpec.fftRls, gpuFFTSize, fftMin);
        }
        else {
            gpuFFT.reset(frameRate, 0.0f, 0.0f, 0.0f, gpuFFTSize, fftMin);
        }
    }

    uint32_t computeFFTSize(int w, int h) {
        if (currSpec.isSizeWidthDependent && initWidth > 0) {
            return (uint32_t)std::round(baseFFTSize * ((float)w / (float)initWidth));
        }
        else if (currSpec.isSizeHeightDependent && initHeight > 0) {
            return (uint32_t)std::round(baseFFTSize * ((float)h / (float)initHeight));
        }
        return baseFFTSize;
    }

    //sets arbitrary size smoothAoS and finds midpoint for the below bin collating algo
    void setIndexFreqs(uint32_t size) {
        indexFreqs.resize(size);
        const float scale = (float)fftSize / (float)sampleRate;
        setSwapFreq(scale);
        bool swapIndexFound = false;
        if (size < 2) return;

        for (uint32_t i = 0; i < size; ++i) {
            float norm = (float)i / (float)(size - 1);
            float freq = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, norm);
            if (!swapIndexFound && freq > swapFreq) {
                swapIndex = i;
                swapIndexFound = true;
                std::cout << "Swap Index: " << swapIndex << std::endl;
            }
            float binIndexFloat = freq * scale;

            indexFreqs[i] = std::min(std::max(binIndexFloat, 0.0f), (float)binAmt);
        }
    }

    void setSwapFreq(const float scale) {
        const float binWidth = 1.0f / scale;
        const float logRatio = std::log(MAX_FREQ / MIN_FREQ);
        swapFreq = binWidth * (float)(indexFreqs.size() - 1) / logRatio;
        swapFreq = std::min(std::max(swapFreq, MIN_FREQ), MAX_FREQ);
        std::cout << "Swap Freq: " << swapFreq << std::endl;
    }

    void linearPlacement() {
        const float* fftOut = audio.getFFTPtr();
        const bool db = currSpec.isFFTdB;
        const bool rms = currSpec.isHighEndRMS;
        if (db && rms) {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, getLowFreqValue(i, fftOut));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, getHighFreqRMSDB(i, fftOut));
            }
        }
        else if (db && !rms) {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, getLowFreqValue(i, fftOut));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, getHighFreqPeak(i, fftOut));
            }

        }
        else if (!db && rms) {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, dBToGain(getLowFreqValue(i, fftOut), MIN_DB));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, getHighFreqRMSGain(i, fftOut));
            }
        }
        else {
            for (int i = 0; i < swapIndex; ++i) {
                gpuFFT.setTargetVal(i, dBToGain(getLowFreqValue(i, fftOut), MIN_DB));
            }
            for (int i = swapIndex; i < gpuFFTSize; ++i) {
                gpuFFT.setTargetVal(i, dBToGain(getHighFreqPeak(i, fftOut), MIN_DB));
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

    //fft gpu low end helper to cubic interpolate with catmull-rom
    float cubicInterp(float y0, float y1, float y2, float y3, float mu) {
        float mu2 = mu * mu;
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 = y1;

        return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Low-end interpolation alternatives
    // All functions:
    //   i         – output index
    //   fftOut    – FFT magnitudes in dB [-96, 0]
    //   returns   – interpolated dB value for that index
    // ─────────────────────────────────────────────────────────────────────────────

    // ── 1. LINEAR ────────────────────────────────────────────────────────────────
    // Simplest baseline. Fast, no overshoot, but audible kinks at peaks/transients.
    float getLowFreqLinear(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        return fftOut[bin1] + t * (fftOut[bin2] - fftOut[bin1]);
    }

    // ── 2. MONOTONIC CUBIC / PCHIP (Fritsch-Carlson) ────────────────────────────
    // Preserves monotonicity between samples – won't overshoot spectral peaks.
    // Generally the safest smooth option for frequency data; recommended default.
    float getLowFreqPCHIP(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0], y1 = fftOut[bin1];
        float y2 = fftOut[bin2], y3 = fftOut[bin3];

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

        return h00*y1 + h10*m1 + h01*y2 + h11*m2;
    }

    // ── 3. WINDOWED SINC / LANCZOS ───────────────────────────────────────────────
    // Excellent reconstruction fidelity. Can ring slightly on very sharp peaks,
    // but on smoothed FFT data this is usually negligible. Kernel radius = 4 bins.
    float getLowFreqLanczos(int i, const float* fftOut) {
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
            sum  += w * fftOut[bin];
            wsum += w;
        }
        return (wsum > 1e-9f) ? sum / wsum : fftOut[center];
    }

    // ── 4. GAUSSIAN ──────────────────────────────────────────────────────────────
    // Weighted average using a Gaussian bell centred on the fractional bin.
    // Very smooth / blurry – good if you want perceptual smoothing of the spectrum,
    // not ideal if you need accurate peak representation.
    float getLowFreqGaussian(int i, const float* fftOut) {
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
            sum  += w * fftOut[bin];
            wsum += w;
        }
        return (wsum > 1e-9f) ? sum / wsum : fftOut[center];
    }

    // ── 5. CUBIC B-SPLINE ────────────────────────────────────────────────────────
    // Smoothes over the control points rather than interpolating them exactly.
    // Very smooth, no overshoot, but values won't hit actual bin magnitudes.
    // Works well if bins are already dense and slight amplitude softening is OK.
    float getLowFreqBSpline(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0], y1 = fftOut[bin1];
        float y2 = fftOut[bin2], y3 = fftOut[bin3];

        float t2 = t * t, t3 = t2 * t;
        float b0 = (1.0f - 3*t + 3*t2 -   t3) / 6.0f;
        float b1 = (4.0f         - 6*t2 + 3*t3) / 6.0f;
        float b2 = (1.0f + 3*t + 3*t2 - 3*t3) / 6.0f;
        float b3 =                          t3  / 6.0f;

        return b0*y0 + b1*y1 + b2*y2 + b3*y3;
    }

    // ── 6. MIN/MAX ENVELOPE ──────────────────────────────────────────────────────
    // Returns a linear blend between the min and max of the surrounding window.
    // Blend factor alpha=0 → min envelope, alpha=1 → max, 0.5 → midpoint.
    // Unique character – can highlight peaks (alpha→1) or troughs.
    float getLowFreqMinMaxEnvelope(int i, const float* fftOut,
                                   float alpha = 1.0f,   // 0=min, 1=max
                                   int   halfWin = 1) {
        float cf = indexFreqs[i];
        uint32_t center = (uint32_t)cf;

        float lo =  1e30f, hi = -1e30f;
        for (int k = -(halfWin); k <= halfWin + 1; ++k) {
            int bin = (int)center + k;
            if (bin < 0) bin = 0;
            if (bin >= (int)binAmt) bin = (int)binAmt - 1;
            lo = std::min(lo, fftOut[bin]);
            hi = std::max(hi, fftOut[bin]);
        }
        return lo + alpha * (hi - lo);
    }

    // ── 7. AKIMA ─────────────────────────────────────────────────────────────────
    // Local cubic that is less prone to oscillation than standard Catmull-Rom
    // (your current method). Uses 5 points; slope weighting suppresses outliers.
    float getLowFreqAkima(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin2 = (uint32_t)cf;   // left bracket
        float t = cf - bin2;

        // Clamp extended neighbourhood
        auto clampBin = [&](int b) -> uint32_t {
            return (uint32_t)std::max(0, std::min((int)binAmt - 1, b));
        };
        float y[5] = {
            fftOut[clampBin((int)bin2 - 2)],
            fftOut[clampBin((int)bin2 - 1)],
            fftOut[clampBin((int)bin2    )],
            fftOut[clampBin((int)bin2 + 1)],
            fftOut[clampBin((int)bin2 + 2)]
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
        return ( 2*t3 - 3*t2 + 1) * y[2]
             + (   t3 - 2*t2 + t) * s1
             + (-2*t3 + 3*t2    ) * y[3]
             + (   t3 -   t2    ) * s2;
    }

    // ── BONUS: LOG-DOMAIN PCHIP ──────────────────────────────────────────────────
    // Same PCHIP as above but converts dB→linear before interpolating, then back.
    // Perceptually more accurate for spectral peaks; energy is conserved in
    // amplitude space rather than dB space. Worth A/B-ing against plain PCHIP.
    float getLowFreqLogPCHIP(int i, const float* fftOut) {
        constexpr float MIN_DB = -96.0f;

        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        // Convert to linear amplitude for interpolation
        float y0 = dBToGain(fftOut[bin0], MIN_DB);
        float y1 = dBToGain(fftOut[bin1], MIN_DB);
        float y2 = dBToGain(fftOut[bin2], MIN_DB);
        float y3 = dBToGain(fftOut[bin3], MIN_DB);

        float d0 = y1 - y0, d1 = y2 - y1, d2 = y3 - y2;

        auto pchipSlope = [](float dm, float dp) -> float {
            if (dm * dp <= 0.0f) return 0.0f;
            float w1 = 2.0f * dp + dm, w2 = dp + 2.0f * dm;
            return (w1 + w2) / (w1 / dm + w2 / dp);
        };

        float m1 = pchipSlope(d0, d1);
        float m2 = pchipSlope(d1, d2);

        float t2 = t*t, t3 = t2*t;
        float h00 =  2*t3 - 3*t2 + 1;
        float h10 =    t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 =    t3 -   t2;

        float linearResult = h00*y1 + h10*m1 + h01*y2 + h11*m2;
        return gainToDB(std::max(linearResult, 1e-10f), MIN_DB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Additional low-end interpolation methods
    // ─────────────────────────────────────────────────────────────────────────────

    // ── 1. COSINE ─────────────────────────────────────────────────────────────────
    // Drop-in linear replacement. Smooths the t parameter through a cosine curve
    // so bin boundaries have zero derivative instead of a kink. No extra samples,
    // no overshoot, nearly free.
    float getLowFreqCosine(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);

        float tc = (1.0f - std::cos(M_PI * t)) * 0.5f;   // remap t
        return fftOut[bin1] + tc * (fftOut[bin2] - fftOut[bin1]);
    }

    // ── 2. HERMITE (tension / bias) ───────────────────────────────────────────────
    // Catmull-Rom generalised with two user parameters:
    //   tension  0 = loose/full curve, 1 = linear (tightens the slopes)
    //   bias    -1 = pull toward previous sample, +1 = pull toward next
    // Catmull-Rom is exactly tension=0, bias=0.
    // Good candidate for a user-facing "curve feel" knob pair.
    float getLowFreqHermiteTB(int i, const float* fftOut,
                              float tension = 0.0f,
                              float bias    = 0.0f) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0], y1 = fftOut[bin1];
        float y2 = fftOut[bin2], y3 = fftOut[bin3];

        float tb = (1.0f - tension);

        // Kochanek-Bartels tangents
        float m1 = 0.5f * tb * ((1.0f + bias) * (y1 - y0) + (1.0f - bias) * (y2 - y1));
        float m2 = 0.5f * tb * ((1.0f + bias) * (y2 - y1) + (1.0f - bias) * (y3 - y2));

        float t2 = t * t, t3 = t2 * t;
        float h00 =  2*t3 - 3*t2 + 1;
        float h10 =    t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 =    t3 -   t2;

        return h00*y1 + h10*m1 + h01*y2 + h11*m2;
    }

    // ── 3. EXPONENTIAL / POWER-LAW ────────────────────────────────────────────────
    // Interpolates in linear amplitude space between the two bracketing bins,
    // then converts back to dB. The exponent shapes the curve:
    //   exp = 1.0  → linear in amplitude (slightly log-shaped in dB)
    //   exp > 1.0  → biased toward y1 (slower attack)
    //   exp < 1.0  → biased toward y2 (faster attack)
    // Cheap, intuitive, mirrors perceptual loudness curves.
    float getLowFreqExpPow(int i, const float* fftOut, float exp = 1.0f) {
        constexpr float MIN_DB = -96.0f;

        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);

        float g1 = dBToGain(fftOut[bin1], MIN_DB);
        float g2 = dBToGain(fftOut[bin2], MIN_DB);

        float tc = (exp == 1.0f) ? t : std::pow(t, exp);
        float result = g1 + tc * (g2 - g1);
        return gainToDB(std::max(result, 1e-10f), MIN_DB);
    }

    // ── 4. STEFFEN ────────────────────────────────────────────────────────────────
    // Monotonic cubic (Steffen 1990). Same no-overshoot guarantee as PCHIP with a
    // simpler slope formula: clamp the harmonic mean of adjacent secants to not
    // exceed 3x either secant. Slightly cheaper than PCHIP.
    float getLowFreqSteffen(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin1 = (uint32_t)cf;
        float t = cf - bin1;

        uint32_t bin0 = (bin1 == 0) ? 0 : bin1 - 1;
        uint32_t bin2 = std::min(binAmt - 1, bin1 + 1);
        uint32_t bin3 = std::min(binAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0], y1 = fftOut[bin1];
        float y2 = fftOut[bin2], y3 = fftOut[bin3];

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

        return h00*y1 + h10*m1 + h01*y2 + h11*m2;
    }

    // ── 5. CATMULL-ROM WITH 5-POINT FINITE DIFFERENCE SLOPES ─────────────────────
    // A quiet upgrade to your existing getLowFreqValue. Replaces the 3-point
    // central difference slope m = (y[i+1]-y[i-1])/2 with the 4th-order accurate
    // 5-point stencil m = (-y[i+2] + 8y[i+1] - 8y[i-1] + y[i-2]) / 12.
    // Same cost as original, noticeably better slope accuracy near spectral peaks.
    float getLowFreqCatmullRom5pt(int i, const float* fftOut) {
        float cf = indexFreqs[i];
        uint32_t bin2 = (uint32_t)cf;   // left bracket (y1 in original naming)
        float t = cf - bin2;

        auto clampBin = [&](int b) -> uint32_t {
            return (uint32_t)std::max(0, std::min((int)binAmt - 1, b));
        };

        float ym2 = fftOut[clampBin((int)bin2 - 2)];
        float ym1 = fftOut[clampBin((int)bin2 - 1)];
        float y0  = fftOut[clampBin((int)bin2    )];
        float y1  = fftOut[clampBin((int)bin2 + 1)];
        float y2  = fftOut[clampBin((int)bin2 + 2)];
        float y3  = fftOut[clampBin((int)bin2 + 3)];

        // 5-point stencil slopes at y0 and y1
        float m0 = (-y2  + 8.0f*y1  - 8.0f*ym1 + ym2) / 12.0f;
        float m1 = (-y3  + 8.0f*y2  - 8.0f*y0  + ym1) / 12.0f;

        float t2 = t * t, t3 = t2 * t;
        float h00 =  2*t3 - 3*t2 + 1;
        float h10 =    t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 =    t3 -   t2;

        return h00*y0 + h10*m0 + h01*y1 + h11*m1;
    }

    // ── 6. FLOATER-HORMANN RATIONAL ───────────────────────────────────────────────
    // Floater-Hormann rational interpolation for the full low-end range.
    // Computes all indices [0, endIndex) in one pass, sharing the weight
    // and sample buffer across all output points.
    //
    //   endIndex    – one past the last low-end index to evaluate
    //   fftOut      – FFT magnitudes in dB [-96, 0]
    //   windowHalf  – half-width of local support in bins (4 is a good default)
    //   d           – blend degree, 0..min(2*windowHalf, n) (3 is a good default)
    //
    // Returns a vector of length endIndex with interpolated dB values.

    std::vector<float> getLowFreqFloaterHormann(int endIndex,
                                                const float* fftOut,
                                                int windowHalf = 4,
                                                int d          = 3) {
        std::vector<float> result(endIndex);
        int n = 2 * windowHalf;
        d = std::min(d, n);

        std::vector<float> x(n + 1);
        std::vector<float> y(n + 1);
        std::vector<float> w(n + 1);

        for (int i = 0; i < endIndex; ++i) {
            float cf = indexFreqs[i];
            uint32_t center = (uint32_t)cf;

            // Fill local sample window
            for (int k = 0; k <= n; ++k) {
                int bin = (int)center - windowHalf + k;
                bin     = std::max(0, std::min((int)binAmt - 1, bin));
                x[k]    = (float)bin;
                y[k]    = fftOut[bin];
            }

            // Floater-Hormann weights
            std::fill(w.begin(), w.end(), 0.0f);
            for (int k = 0; k <= n - d; ++k) {
                float binom = 1.0f;
                for (int j = 0; j <= d; ++j) {
                    w[k + j] += binom;
                    binom *= (float)(d - j) / (float)(j + 1);
                }
            }
            for (int k = 0; k <= n; ++k)
                if (k % 2 == 1) w[k] = -w[k];

            // Barycentric rational evaluation at cf
            float num = 0.0f, den = 0.0f;
            for (int k = 0; k <= n; ++k) {
                float diff = cf - x[k];
                if (std::abs(diff) < 1e-10f) { num = y[k]; den = 1.0f; break; }
                float wk = w[k] / diff;
                num += wk * y[k];
                den += wk;
            }
            result[i] = num / den;
        }

        return result;
    }

    float getHighFreqRMSDB(int idx, const float* fftOut) {
        int lowB = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
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
    float getHighFreqRMSGain(int idx, const float* fftOut) {
        int lowB = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float mag = dBToGain(fftOut[i], MIN_DB);
            sumSq += mag * mag;
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return rms;
    }

    float getHighFreqPeak(int idx, const float* fftOut) {
        int lowB = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];

        float peak = MIN_DB;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            peak = std::max(fftOut[i], peak);
        }
        return peak;
    }

    // ── 2. MEDIAN ─────────────────────────────────────────────────────────────────
    float getHighFreqMedian(int idx, const float* fftOut) {
        int lowB  = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];
        int count = highB - lowB + 1;

        if (count == 1) return fftOut[lowB];

        constexpr int STACK_MAX = 64;
        float  stackBuf[STACK_MAX];
        std::vector<float> heapBuf;
        float* buf;

        if (count <= STACK_MAX) {
            buf = stackBuf;
        } else {
            heapBuf.resize(count);
            buf = heapBuf.data();
        }

        for (int k = 0; k < count; ++k)
            buf[k] = fftOut[lowB + k];

        int mid = count / 2;
        std::nth_element(buf, buf + mid, buf + count);

        if (count % 2 == 0) {
            float upper = *std::min_element(buf + mid, buf + count);
            return (buf[mid - 1] + upper) * 0.5f;
        }
        return buf[mid];
    }

    // ── 3. POWER-WEIGHTED MEAN ───────────────────────────────────────────────────
    float getHighFreqPowerWeighted(int idx, const float* fftOut) {
        int lowB  = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];
        int count = highB - lowB + 1;

        if (count == 1) return fftOut[lowB];

        float weightedSum = 0.0f;
        float weightSum   = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float gain  = dBToGain(fftOut[i], MIN_DB);
            float power = gain * gain;
            weightedSum += power * fftOut[i];
            weightSum   += power;
        }
        return (weightSum > 1e-30f) ? weightedSum / weightSum : MIN_DB;
    }

    // ── 4. L-NORM ────────────────────────────────────────────────────────────────
    float getHighFreqLNorm(int idx, const float* fftOut, float p = 2.0f) {
        int lowB  = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];
        int count = highB - lowB + 1;

        if (count == 1) return fftOut[lowB];

        float sum = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float gain = dBToGain(fftOut[i], MIN_DB);
            sum += std::pow(gain, p);
        }
        float result = std::pow(sum / (float)count, 1.0f / p);
        return gainToDB(std::max(result, 1e-10f), MIN_DB);
    }

    // ── 5. TRIMMED RMS ───────────────────────────────────────────────────────────
    float getHighFreqTrimmedRMS(int idx, const float* fftOut,
                                float trimRatio = 0.1f) {
        int lowB  = (idx > 0) ? (int)indexFreqs[idx - 1] + 1 : 0;
        int highB = (int)indexFreqs[idx];
        int count = highB - lowB + 1;

        if (count == 1) return fftOut[lowB];

        int trimN = (int)(count * trimRatio);

        // Fall back to standard RMS if trim would consume all bins
        if (count - 2 * trimN < 1) {
            float sumSq = 0.0f;
            for (int i = lowB; i <= highB && i < fftSize; ++i) {
                float g = dBToGain(fftOut[i], MIN_DB);
                sumSq += g * g;
            }
            return gainToDB(std::sqrt(sumSq / (float)count), MIN_DB);
        }

        constexpr int STACK_MAX = 64;
        float  stackBuf[STACK_MAX];
        std::vector<float> heapBuf;
        float* buf;

        if (count <= STACK_MAX) {
            buf = stackBuf;
        } else {
            heapBuf.resize(count);
            buf = heapBuf.data();
        }

        for (int k = 0; k < count; ++k)
            buf[k] = fftOut[lowB + k];

        std::sort(buf, buf + count);

        float sumSq = 0.0f;
        int   used  = 0;
        for (int k = trimN; k < count - trimN; ++k) {
            float g = dBToGain(buf[k], MIN_DB);
            sumSq += g * g;
            ++used;
        }
        return gainToDB(std::sqrt(sumSq / (float)used), MIN_DB);
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

    uint32_t baseFFTSize = 0;
    uint32_t gpuFFTSize = 0;
    uint32_t swapIndex = 0;

    int initWidth = 0;
    int initHeight = 0;
    int currentWidth = 0;
    int currentHeight = 0;

    float swapFreq = 0.0f;
};

