#pragma once

#include <atomic>
#include <cmath>
#include <vector>
#include <fftw3.h>

#define PI 3.14159265358979323846f

//peak measurement per channel
struct Peak {
    float pop() {
        return value.exchange(0.0f);
    }

    float peek() {
        return value.load();
    }

    //only called in process block per channel. gets peak and calls update per block
    void getPeakFromBlock(const float* block, const int numSamples, const int channelNum, const int channelAmount) {
        float peakValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            float absSample = std::abs(block[i]);
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
        while (newValue > oldValue && !value.compare_exchange_weak(oldValue, newValue, std::memory_order_relaxed, std::memory_order_relaxed));
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

    void getRMSFromBlock(const float* block, const int numSamples, const int channelNum, const int channelAmount) {
        float rmsValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            rmsValue += block[i] * block[i];
        }
        rmsValue /= numSamples;
        rmsValue = std::sqrt(rmsValue);
        value.store(rmsValue);
    }

private:
    std::atomic<float> value{ 0.0f };
};

struct FFT{
    FFT(int N, bool per, bool win, bool dB, float sr, float slope = 0.0f) : 
        n(N), isPerceptual(per), isWindowed(win), isDB(dB), perceptualSlope(slope) {
        //initialize fft and precompute table
        in = (float *)fftwf_malloc(sizeof(float) * n);
        out = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * (n / 2 + 1));
        p = fftwf_plan_dft_r2c_1d(n, in, out, FFTW_ESTIMATE);
        placement = (float *) out;
        if (isWindowed) {
            windowingTable.resize(n);
            fillWindowingTable();
        }
        scalarTable.resize(n / 2 + 1);
        fillScalarTable(sr);
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
                                isPerceptual(other.isPerceptual),
                                isWindowed(other.isWindowed), isDB(other.isDB),
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
            isPerceptual = other.isPerceptual;
            isWindowed = other.isWindowed;
            isDB = other.isDB;
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

    float* getInputBuffer() {
        return in;
    }

    float* getOutputBuffer() {
        return placement;
    }

    void runFFT() {
        if (isWindowed) {
            multiplyWithWindowingTable();
        }
        fftwf_execute(p);
        convertToPower();
        multiplyWithScalarTable();
        if (isDB) {
            convertToDB();
        }
    }

private:
    void fillWindowingTable() {
        //fill table with hann window scalars
        for (int i = 0; i < n; ++i) {
            auto cos2 = std::cos(2 * i * PI / (n - 1));
            windowingTable[i] = 0.5f - 0.5f * cos2;
        }

        //get sum for normalize
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            sum += windowingTable[i];
        }

        //normalize
        auto factor = (float)n / sum;
        for (int i = 0; i < n; ++i) {
            windowingTable[i] *= factor;
        }
    }

    void fillScalarTable(float sr) {
        const float winScale = (isWindowed) ? 2.0f : 1.0f;
        const float pScale = winScale / (float)((double)n * (double)n);
        const float tiltExponent = (isPerceptual) ? perceptualSlope / 6.0206f : 0.0f;
        const float binMult = sr / (float)n;
        const int binAmt = n / 2 + 1;
        for (int i = 0; i < binAmt; ++i) {
            float binFreq = (float)i * binMult;
            float tilt = std::pow(binFreq / 1000.0f, tiltExponent);
            scalarTable[i] = tilt * pScale;
        }
    }

    void multiplyWithWindowingTable() {
        for (int i = 0; i < n; ++i) {
            in[i] *= windowingTable[i];
        }
    }

    void convertToPower() {
        const int binAmt = n / 2 + 1;
        for (int i = 0; i < binAmt; ++i) {
            float real = out[i][0];
            float imag = out[i][1];
            float power = real * real + imag * imag;
            placement[i] = power;
        }
    }

    void multiplyWithScalarTable() {
        const int binAmt = n / 2 + 1;
        for (int i = 0; i < binAmt; ++i) {
            placement[i] *= scalarTable[i];
        }
    }

    void convertToDB() {
        const float min_power = 1e-12f;
        const int binAmt = n / 2 + 1;
        for (int i = 0; i < binAmt; ++i) {
            float power = std::max(placement[i], min_power);
            placement[i] = 10.0f * std::log10(power);
        }
    }

    float *in;
    fftwf_complex *out;
    float *placement;
    fftwf_plan p;
    bool isPerceptual;
    bool isWindowed;
    bool isDB;
    float perceptualSlope;
    int n;
    std::vector<float> windowingTable;
    std::vector<float> scalarTable;
};

