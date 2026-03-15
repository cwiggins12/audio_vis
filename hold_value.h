#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

//need equivalent maths for handling dB values with the linear scalar
//if you go with default construction, you must reset, or you'll have a bad time
class HoldArray {
public:
    HoldArray() = default;

    HoldArray(uint32_t sampleRate, float rampLengthInSecs, size_t size, 
              float scalar, bool isDB) {
        resetConfig(sampleRate, rampLengthInSecs, scalar, isDB);
        resize(size);
        resetVals();
    }

    HoldArray(uint32_t steps, size_t size, float scalar, bool isDB) {
        resetConfig(steps, scalar, isDB);
        resize(size);
        resetVals();
    }

    // Copy constructor
    HoldArray(const HoldArray& other) : values(other.values), 
              countdowns(other.countdowns), maxSteps(other.maxSteps), 
              linearDropScalar(other.linearDropScalar), minValue(other.minValue) {}

    // Copy assignment
    HoldArray& operator=(const HoldArray& other) {
        if (this != &other) {
            values = other.values;
            countdowns = other.countdowns;
            maxSteps = other.maxSteps;
            linearDropScalar = other.linearDropScalar;
            minValue = other.minValue;
        }
        return *this;
    }

    // Move constructor
    HoldArray(HoldArray&& other) noexcept : values(std::move(other.values)), 
              countdowns(std::move(other.countdowns)), maxSteps(other.maxSteps), 
              linearDropScalar(other.linearDropScalar), minValue(other.minValue) {}

    // Move assignment
    HoldArray& operator=(HoldArray&& other) noexcept {
        if (this != &other) {
            values = std::move(other.values);
            countdowns = std::move(other.countdowns);
            maxSteps = other.maxSteps;
            linearDropScalar = other.linearDropScalar;
            minValue = other.minValue;
        }
        return *this;
    }

    ~HoldArray() = default;

    void reset(uint32_t sampleRate, float rampLengthInSecs, float scalar, 
                  bool isDB, size_t newSize) {
        resetConfig(sampleRate, rampLengthInSecs, scalar, isDB);
        resize(newSize);
    }

    void reset(int steps, float scalar, bool isDB, size_t newSize) {
        resetConfig(steps, scalar, isDB);
        resize(newSize);
    }

    void resize(size_t newSize) {
        values.resize(newSize);
        countdowns.resize(newSize);
        resetVals();
    }

    void compareValAtIndex(uint32_t i, float val) {
        if (values[i] <= val) {
            values[i] = val;
            countdowns[i] = maxSteps;
        }
    }

    //doesn't bounds check, better be sure sizes are the same
    void compareValsToArray(const float* arr, float val) {
        int n = values.size();
        for (int i = 0; i < n; ++i) {
            compareValAtIndex(i, arr[i]);
        }
    }

    void countdownIndex(uint32_t i) {
        if (countdowns[i] >= 0) {
            countdowns[i]--;
        }
        else {
            float val = values[i] - minValue;
            val *= linearDropScalar;
            val += minValue;
            values[i] = val;
        }
    }

    void countdownAll() {
        int n = values.size();
        for (int i = 0; i < n; ++i) {
            countdownIndex(i);
        }
    }

    //use this to reset values without resizing or resetting
    void resetVals() {
        std::fill(values.begin(), values.end(), minValue);
        std::fill(countdowns.begin(), countdowns.end(), maxSteps);
    }

    float* getValuePtr() {
        return values.data();
    }

private:
    void resetConfig(int steps, float scalar, bool isDB) {
        maxSteps = steps;
        minValue = (isDB) ? -96.0f : 0.0f;
        linearDropScalar = scalar;
    }

    void resetConfig(uint32_t sampleRate, float rampLengthInSecs,
                     float scalar, bool isDB) {
        resetConfig((int)std::floor(rampLengthInSecs * (float)sampleRate), 
                     scalar, isDB);
    }

    std::vector<float> values;
    std::vector<int> countdowns;

    uint32_t maxSteps;
    float linearDropScalar;
    float minValue;
};

