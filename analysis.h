#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <fftw3.h>

static constexpr float PI = 3.14159265358979323846f;

//peak measurement per channel
struct Peak {
    float pop() {
        return value.exchange(0.0f);
    }

    float peek() {
        return value.load();
    }

    //only called in process block per channel. gets peak and calls update per block
    void getPeakFromBlock(const float* block, const int numSamples, 
                          const int channelNum, const int channelAmount) {
        float peakValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            float absSample = std::abs(block[i]);
            if (absSample > peakValue) {
                peakValue = absSample;
            }
        }
        update(peakValue);
    }

    void getPeakFromRingBuffer(const float* buffer, const int numSamples, 
                               const int channelNum, const int channelAmount, 
                               const int start, const int buffSize) {
        float peakValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            int idx = (i + start) % buffSize;
            float absSample = std::abs(buffer[idx]);
            if (absSample > peakValue) {
                peakValue = absSample;
            }
        }
        update(peakValue);
    }

private:
    std::atomic<float> value{ 0.0f };
    //write peak from block to value
    void update(float newValue) {
        auto oldValue = value.load(std::memory_order_relaxed);
        while (newValue > oldValue && 
               !value.compare_exchange_weak(oldValue, newValue, 
               std::memory_order_relaxed, std::memory_order_relaxed));
    }
};

//rms reading per channel
struct RMS {
    float pop() {
        return value.exchange(0.0f);
    }

    float peek() {
        return value.load();
    }

    void getRMSFromBlock(const float* block, const int numSamples, 
                         const int channelNum, const int channelAmount) {
        float rmsValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            rmsValue += block[i] * block[i];
        }
        rmsValue /= numSamples;
        rmsValue = std::sqrt(rmsValue);
        value.store(rmsValue);
    }

    void getRMSFromRingBuffer(const float* buffer, const int numSamples, 
                              const int channelNum, const int channelAmount, 
                              const int start, const int buffSize) {
        float rmsValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            int idx = (i + start) % buffSize;
            rmsValue += buffer[idx] * buffer[idx];
        }
        rmsValue /= numSamples;
        rmsValue = std::sqrt(rmsValue);
        value.store(rmsValue);
    }

private:
    std::atomic<float> value{ 0.0f };
};

struct FFT{
    FFT(int N, bool per = true, bool win = true, 
        bool dB = true, bool ss = true, float slope = 0.0f) : 
        n(N), isPerceptual(per), isWindowed(win), 
        isDB(dB), isSingleSided(ss), perceptualSlope(slope) {
        binAmt = n / 2 + 1;
        //initialize fft and precompute table
        in = (float *)fftwf_malloc(sizeof(float) * n);
        out = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * binAmt);
        p = fftwf_plan_dft_r2c_1d(n, in, out, FFTW_MEASURE);
        placement = (float *)out;
        if (isWindowed) {
            windowingTable.resize(n);
        }
        scalarTable.resize(binAmt);
    }

    ~FFT() {
        if (p)      fftwf_destroy_plan(p);
        if (in)     fftwf_free(in);
        if (out)    fftwf_free(out);
    }

    //no copies allowed
    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;

    //move is ok for the potential of placing these in vectors
    FFT(FFT&& other) noexcept : in(other.in), out(other.out), p(other.p),
                                placement(other.placement), n(other.n),
                                binAmt(other.binAmt), isPerceptual(other.isPerceptual),
                                isWindowed(other.isWindowed), isDB(other.isDB),
                                isSingleSided(other.isSingleSided),
                                perceptualSlope(other.perceptualSlope),
                                windowingTable(std::move(other.windowingTable)),
                                scalarTable(std::move(other.scalarTable)) {
        other.in = nullptr;
        other.out = nullptr;
        other.p = nullptr;
        other.placement = nullptr;
    }
    FFT& operator=(FFT&& other) noexcept {
        if (this != &other) {
            fftwf_destroy_plan(p);
            fftwf_free(in);
            fftwf_free(out);

            in = other.in;
            out = other.out;
            p = other.p;
            placement = other.placement;
            n = other.n;
            binAmt = other.binAmt;
            isPerceptual = other.isPerceptual;
            isWindowed = other.isWindowed;
            isDB = other.isDB;
            isSingleSided = other.isSingleSided;
            perceptualSlope = other.perceptualSlope;
            windowingTable = std::move(other.windowingTable);
            scalarTable = std::move(other.scalarTable);
            other.in = nullptr;
            other.out = nullptr;
            other.p = nullptr;
            other.placement = nullptr;
        }
        return *this;
    }

    void initFFT(uint32_t sr) {
        fillScalarTable(sr);
        if (isWindowed) {
            fillWindowingTable();
        }
    }

    float* getInputBuffer() {
        return in;
    }

    float* getOutputBuffer() {
        return placement;
    }

    uint32_t getTotalBins() {
        return binAmt;
    }

    //return first index and amount of bins used in spectral analysis
    std::vector<uint32_t> getAudibleRange(uint32_t sr) {
        const float binMult = (float)sr / (float) n;
        bool firstAudibleSet = false;
        std::vector<uint32_t> res = {0, 0};

        for (int i = 0; i < binAmt; ++i) {
            float binFreq = (float)i * binMult;
            if (!firstAudibleSet && binFreq >= 20.0f) {
                res[0] = i;
                firstAudibleSet = true;
            }
            if (binFreq > 20000.0f) {
                res[1] = i - res[0];
                break;
            }
        }
        return res;
    }

    void runFFT() {
        if (isWindowed) {
            multiplyWithWindowingTable();
        }
        fftwf_execute(p);
        convertToMag();
        multiplyWithScalarTable();
        if (isDB) {
            convertToDB();
        }
    }

private:
    void fillWindowingTable() {
        //fill table with hann window scalars
        for (uint32_t i = 0; i < n; ++i) {
            auto cos2 = std::cos(2 * i * PI / (n - 1));
            windowingTable[i] = 0.5f - 0.5f * cos2;
        }

        //get sum for normalize
        float sum = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            sum += windowingTable[i];
        }

        //normalize
        auto factor = (float)n / sum;
        for (uint32_t i = 0; i < n; ++i) {
            windowingTable[i] *= factor;
        }
    }

    void fillScalarTable(uint32_t sr) {
        //NOTE: accounts for single sided and loss from FFT ops at 2.0f each. 
        //Scale has been tested, it is consistently in line with peak and RMS
        float scaleNumerator = (isSingleSided) ? 4.0f : 2.0f;
        const float scale = scaleNumerator / (float)n;
        const float tiltExponent = (isPerceptual) ? perceptualSlope / 6.0206f : 0.0f;
        const float binMult = (float)sr / (float)n;
        bool firstAudibleSet = false;
        bool lastAudibleSet = false;

        scalarTable[0] = scale / 2.0f;
        for (uint32_t i = 1; i < binAmt - 1; ++i) {
            float binFreq = (float)i * binMult;
            float tilt = std::pow(binFreq / 1000.0f, tiltExponent);
            scalarTable[i] = tilt * scale;
        }
        //annoying
        scalarTable[binAmt - 1] = std::pow(((float)(binAmt - 1) * binMult) / 1000.0f, 
                                             tiltExponent) * (scale / 2);
    }

    void multiplyWithWindowingTable() {
        for (uint32_t i = 0; i < n; ++i) {
            in[i] *= windowingTable[i];
        }
    }

    void convertToMag() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            float real = out[i][0];
            float imag = out[i][1];
            float mag = std::sqrt(real * real + imag * imag);
            placement[i] = mag;
        }
    }

    void multiplyWithScalarTable() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            placement[i] *= scalarTable[i];
        }
    }

    void convertToDB() {
        const float min_mag = 1e-12f;
        for (uint32_t i = 0; i < binAmt; ++i) {
            float mag = std::max(placement[i], min_mag);
            mag = 20.0f * std::log10(mag);
            placement[i] = std::max(mag, -120.0f);
        }
    }

    //NOTE: 
    //total byte size: ((3n + 1) * (n / 2 + 1) * sizeof(float)) + 82 + sizeof(plan) 

    //size: n * sizeof(float)
    float *in;
    //size: (n / 2 + 1) * sizeof(complex(8 bytes))
    fftwf_complex *out;
    //pointer to overwrite complexes after conversions
    float *placement;
    fftwf_plan p;
    bool isPerceptual;
    bool isWindowed;
    bool isDB;
    bool isSingleSided;
    float perceptualSlope;
    uint32_t n;
    uint32_t binAmt;
    //size: n * sizeof(float)
    std::vector<float> windowingTable;
    //size: (n / 2 + 1) * sizeof(float)
    std::vector<float> scalarTable;
};

