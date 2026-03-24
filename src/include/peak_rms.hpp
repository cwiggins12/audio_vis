#pragma once

#include <atomic>
#include <cmath>

//peak measurement per channel
struct PeakMeter {
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

    void getPeakFromMonoSummedBlock(const float* block, const int numSamples) {
        float peakValue = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
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

    void getMonoSummedPeakFromRingBuffer(const float* buffer, const int numSamples,
                                 const int channelAmount, const int start, 
                                 const int buffSize) {
        float peakValue = 0.0f;
        for (int i = 0; i < numSamples * channelAmount; i += channelAmount) {
            float frameSum = 0.0f;
            for (int ch = 0; ch < channelAmount; ++ch) {
                int idx = (i + ch + start) % buffSize;
                frameSum += buffer[idx];
            }
            frameSum /= channelAmount;
            float absFrame = std::abs(frameSum);
            if (absFrame > peakValue) {
                peakValue = absFrame;
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
struct RMSMeter {
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

    void getRMSFromMonoSummedBlock(const float* block, const int numSamples) {
        float rmsValue = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
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

    void getRMSFromMonoSummedRingBuffer(const float* buffer, const int numSamples, 
                                        const int channelAmount, const int start,
                                        const int buffSize) {
        float rmsValue = 0.0f;
        for (int i = 0; i < numSamples * channelAmount; i += channelAmount) {
            float frameSum = 0.0f;
            for (int ch = 0; ch < channelAmount; ++ch) {
                int idx = (i + ch + start) % buffSize;
                frameSum += buffer[idx];
            }
            frameSum /= channelAmount;
            rmsValue += frameSum * frameSum;
        }
        rmsValue /= numSamples;
        rmsValue = std::sqrt(rmsValue);
        value.store(rmsValue);
    }

private:
    std::atomic<float> value{ 0.0f };
};

