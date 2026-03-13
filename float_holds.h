#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

class HoldArray {
public:
    HoldArray() = default;

    HoldArray(uint32_t sampleRate, float rampLengthInSeconds, size_t size,
              float scalar, float minValue) {
        reset(rampLengthInSeconds, sampleRate);
        resize(size, minValue);
        linearDropScalar = scalar;
    }

    HoldArray(uint32_t max, size_t size, float scalar, float minValue) {
        reset(max);
        resize(size, minValue);
        linearDropScalar = scalar;
    }

    void resize(size_t newSize, float minValue) {
        values.resize(newSize, minValue);
        countdowns.resize(newSize, maxSteps);
    }

    void reset(int steps) {
        maxSteps = steps;
    }

    void reset(uint32_t sampleRate, float rampLengthInSeconds) {
        reset((int)std::floor(rampLengthInSeconds * (float)sampleRate));
    }

    void compareValAtIndex(uint32_t i, float val) {
        if (values[i] <= val) {
            values[i] = val;
            countdowns[i] = maxSteps;
        }
    }

    void countdownIndex(uint32_t i) {
        (countdowns[i] > 0) ? countdowns[i]-- : values[i] *= linearDropScalar;
    }

    void countdownAll() {
        int n = values.size();
        for (int i = 0; i < n; ++i) {
            (countdowns[i] > 0) ? countdowns[i]-- : values[i] *= linearDropScalar;
        }
    }

    void clear(float minVal) {
        int n = values.size();
        for (int i = 0; i < n; ++i) {
            values[i] = minVal;
            countdowns[i] = maxSteps;
        }
    }

private:
    std::vector<float> values;
    std::vector<int> countdowns;

    uint32_t maxSteps;
    float linearDropScalar;
};

