#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

//need equivalent maths for handling dB values with the linear scalar
class HoldArray {
public:
    HoldArray() = default;

    HoldArray(uint32_t sampleRate, float rampLengthInSeconds, size_t size,
              float scalar, float min) {
        reset(rampLengthInSeconds, sampleRate, min);
        resize(size);
        linearDropScalar = scalar;
    }

    HoldArray(uint32_t steps, size_t size, float scalar, float min) {
        reset(steps, scalar, min);
        resize(size);
        linearDropScalar = scalar;
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

    void resize(size_t newSize) {
        values.resize(newSize, minValue);
        countdowns.resize(newSize, maxSteps);
    }

    void reset(int steps, float scalar, float min) {
        maxSteps = steps;
        minValue = min;
        linearDropScalar = scalar;
    }

    void reset(uint32_t sampleRate, float rampLengthInSeconds,
               float scalar, float min) {
        reset((int)std::floor(rampLengthInSeconds * (float)sampleRate), scalar, min);
    }

    void compareValAtIndex(uint32_t i, float val) {
        if (values[i] <= val) {
            values[i] = val;
            countdowns[i] = maxSteps;
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

    void clear() {
        int n = values.size();
        for (int i = 0; i < n; ++i) {
            values[i] = minValue;
            countdowns[i] = maxSteps;
        }
    }

    void setMinValBasedOnSpecBool(bool isDB) {
        //will fix l8r. h8 the magic nums
        minValue = (isDB) ? -96.0f : 0.0f;
    }

    float* getValuePtr() {
        return values.data();
    }

private:
    std::vector<float> values;
    std::vector<int> countdowns;

    uint32_t maxSteps;
    float linearDropScalar;
    float minValue;
};

