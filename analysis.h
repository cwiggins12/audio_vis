#pragma once

#include <atomic>
#include <cmath>
#include <vector>
#include <fftw3.h>

#define PI 3.14159265358979323846f

//peak measurement per channel
struct Peak {
    //destructive read
    float read() noexcept {
        return value.exchange(0.0f);
    }
    //only called in process block per channel. gets peak and calls update per block
    void getPeakFromBlock(const float* block, const int numSamples, const int channelNum, const int channelAmount) {
        float peakValue = 0.0f;
        for (int i = channelNum; i < numSamples; i += channelAmount) {
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
    void update(float newValue) noexcept {
        auto oldValue = value.load(std::memory_order_relaxed);
        while (newValue > oldValue && !value.compare_exchange_weak(oldValue, newValue, std::memory_order_release, std::memory_order_relaxed));
    }
};

//rms reading per channel
struct RMS {
    float read() noexcept {
        return value.exchange(0.0f);
    }
    void getRMSFromBlock(const float* block, const int numSamples, const int channelNum, const int channelAmount) {
        float rmsValue = 0.0f;
        for (int i = channelNum; i < numSamples * channelAmount; i += channelAmount) {
            rmsValue += block[i] * block[i];
        }
        rmsValue /= numSamples;
        rmsValue = std::sqrt(rmsValue);
        update(rmsValue);
    }

private:
    std::atomic<float> value{ 0.0f };
    void update(float newValue) noexcept {
        auto oldValue = value.load(std::memory_order_relaxed);
        while (newValue > oldValue && !value.compare_exchange_weak(oldValue, newValue, std::memory_order_release, std::memory_order_relaxed));
    }
};

struct FFT{
    FFT(int N) {
        //initialize fft and windowing table
        in = (float *)fftwf_malloc(sizeof(float) * N);
        out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (N / 2 + 1));
        p = fftwf_plan_dft_r2c_1d(N, in, out, FFTW_ESTIMATE);
        windowingTable.resize(N);
        fillWindowingTable(N);
    }
    ~FFT() {
        fftwf_destroy_plan(p);
        fftwf_free(in);
        fftwf_free(out);
    }
    float* getInputArray() {
        return in;
    }
    //add scalars to windowingTable later
    void runFFT() {
        multiplyWithWindowingTable();
        fftwf_execute(p);
    }

private:
    void fillWindowingTable(int size) {
        //fill table with hann window scalars
        for (int i = 0; i < size; ++i) {
            auto cos2 = std::cos(2 * i * PI / (size - 1));
            windowingTable[i] = 0.5f - 0.5f * cos2;
        }
        //get sum for normalize
        float sum = 0.0f;
        for (int i = 0; i < size; ++i) {
            sum += windowingTable[i];
        }
        //normalize
        auto factor = static_cast<float>(size) / sum;
        for (int i = 0; i < size; ++i) {
            windowingTable[i] *= factor;
        }
    }

    void multiplyWithWindowingTable() {
        for (int i = 0; i < windowingTable.size(); ++i) {
            in[i] *= windowingTable[i];
        }
    }

    float *in;
    fftwf_complex *out;
    fftwf_plan p;
    std::vector<float> windowingTable;
};

