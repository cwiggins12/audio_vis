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

    // Copy constructor
    HoldArray(const HoldArray& other) : values(other.values), 
              countdowns(other.countdowns), maxSteps(other.maxSteps), 
              linearDropScalar(other.linearDropScalar) {}

    // Copy assignment
    HoldArray& operator=(const HoldArray& other) {
        if (this != &other) {
            values           = other.values;
            countdowns       = other.countdowns;
            maxSteps         = other.maxSteps;
            linearDropScalar = other.linearDropScalar;
        }
        return *this;
    }

    // Move constructor
    HoldArray(HoldArray&& other) noexcept : values(std::move(other.values)), 
              countdowns(std::move(other.countdowns)), maxSteps(other.maxSteps), 
              linearDropScalar(other.linearDropScalar) {}

    // Move assignment
    HoldArray& operator=(HoldArray&& other) noexcept {
        if (this != &other) {
            values           = std::move(other.values);
            countdowns       = std::move(other.countdowns);
            maxSteps         = other.maxSteps;
            linearDropScalar = other.linearDropScalar;
        }
        return *this;
    }

    ~HoldArray() = default;

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

    float* getValuePtr() {
        return values.data();
    }

private:
    std::vector<float> values;
    std::vector<int> countdowns;

    uint32_t maxSteps;
    float linearDropScalar;
};

