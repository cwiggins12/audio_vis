#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

// SoA layout version of SmoothArray for direct GPU buffer access
//if using default, call reset. If needing separated config calls,
//call setAsym(), resize()
struct SmoothArraySoA {
    //left for use as a member variable, but make sure to 
    //run reset() ASAP or all hell breaks loose
    SmoothArraySoA() = default;

    SmoothArraySoA(uint32_t sampleRate, float rampLengthInSecs, size_t size,
                   float atkSecs = -1.0f, float rlsSecs = -1.0f) {
        atkSecs = (atkSecs < 0.0f) ? rampLengthInSecs : atkSecs;
        rlsSecs = (rlsSecs < 0.0f) ? rampLengthInSecs : rlsSecs;
        setAsym(sampleRate, atkSecs, rlsSecs);
        resize(size);
    }

    SmoothArraySoA(int steps, size_t size, int atk = -1, int rls = -1) {
        atk = (atk < 0) ? steps : atk;
        rls = (rls < 0) ? steps : rls;
        setAsym(atk, rls);
        resize(size);
    }

    // Copy constructor
    SmoothArraySoA(const SmoothArraySoA& other) : current(other.current), 
                   target(other.target), increment(other.increment), 
                   stepsRemaining(other.stepsRemaining),atkSteps(other.atkSteps), 
                   rlsSteps(other.rlsSteps), minVal(other.minVal) {}

    // Copy assignment
    SmoothArraySoA& operator=(const SmoothArraySoA& other) {
        if (this != &other) {
            current = other.current;
            target = other.target;
            increment = other.increment;
            stepsRemaining = other.stepsRemaining;
            atkSteps = other.atkSteps;
            rlsSteps = other.rlsSteps;
            minVal = other.minVal;
        }
        return *this;
    }

    // Move constructor
    SmoothArraySoA(SmoothArraySoA&& other) noexcept : current(std::move(other.current)),
                   target(std::move(other.target)),
                   increment(std::move(other.increment)), 
                   stepsRemaining(std::move(other.stepsRemaining)), 
                   atkSteps(other.atkSteps), rlsSteps(other.rlsSteps),
                   minVal(other.minVal) {}

    // Move assignment
    SmoothArraySoA& operator=(SmoothArraySoA&& other) noexcept {
        if (this != &other) {
            current = std::move(other.current);
            target = std::move(other.target);
            increment = std::move(other.increment);
            stepsRemaining = std::move(other.stepsRemaining);
            atkSteps = other.atkSteps;
            rlsSteps = other.rlsSteps;
            minVal = other.minVal;
        }
        return *this;
    }

    ~SmoothArraySoA() = default;

    void reset(uint32_t sampleRate, float rampLengthInSecs, float atkSecs, 
               float rlsSecs, size_t newSize, float min) {
        setAsym(sampleRate, atkSecs, rlsSecs);
        resize(newSize, min);
    }

    void reset(uint32_t steps, int atk, int rls, 
               size_t newSize, float min = 0.0f) {
        setAsym(atk, rls);
        resize(newSize, min);
    }

    void setAsym(uint32_t sampleRate, float atkSecs, float rlsSecs) {
        atkSteps = (int)std::floor(atkSecs * (float)sampleRate);
        rlsSteps = (int)std::floor(rlsSecs * (float)sampleRate);
    }

    void setAsym(int atk, int rls) {
        atkSteps = atk;
        rlsSteps = rls;
    }

    void resize(size_t newSize) {
        current.resize(newSize);
        target.resize(newSize);
        increment.resize(newSize);
        stepsRemaining.resize(newSize);
        resetAllVals();
    }

    void resize(size_t newSize, float newMin) {
        minVal = newMin;
        current.resize(newSize);
        target.resize(newSize);
        increment.resize(newSize);
        stepsRemaining.resize(newSize);
        resetAllVals();
    }

    //single index setters

    void setCurrentAndTargetVal(size_t i, float val) {
        current[i] = val;
        target[i] = val;
        stepsRemaining[i] = 0;
        increment[i] = 0.0f;
    }

    void setCurrentToTargetVal(size_t i) {
        current[i] = target[i];
        stepsRemaining[i] = 0;
        increment[i] = 0.0f;
    }

    void setTargetVal(size_t i, float val) {
        if (target[i] == val) return;
        target[i] = val;
        int steps = (val > current[i]) ? atkSteps : rlsSteps;
        if (steps <= 0) {
            setCurrentAndTargetVal(i, val);
            return;
        }
        stepsRemaining[i] = steps;
        increment[i] = (val - current[i]) / (float)steps;
    }

    //single index getters

    float getCurrentVal(size_t i) const { return current[i]; }
    float getTargetVal(size_t i)  const { return target[i];  }
    bool  isSmoothing(size_t i)   const { return stepsRemaining[i] > 0; }

    float getNextVal(size_t i) {
        if (!isSmoothing(i)) return target[i];
        --stepsRemaining[i];
        if (isSmoothing(i)) {
            current[i] += increment[i];
        }
        else {
            current[i] = target[i];
        }
        return current[i];
    }

    //bulk ops

    void advanceAll() {
        for (size_t i = 0; i < current.size(); i++) {
            getNextVal(i);
        }
    }

    void setAllTargetsWithVal(float val) {
        for (size_t i = 0; i < current.size(); i++) {
            setTargetVal(i, val);
        }
    }

    //NOTE: DOES NOT CHECK SIZE OF IN ARRAY. SIZE MATCHING IS UP TO CALLER
    void setAllTargetsWithPtr(float* in) {
        for (size_t i = 0; i < current.size(); ++i) {
            setTargetVal(i, in[i]);
        }
    }

    void setAllCurrentAndTargets(float val) {
        for (size_t i = 0; i < current.size(); i++) {
            setCurrentAndTargetVal(i, val);
        }
    }

    //getters

    const float* getCurrents()  const { return current.data(); }
    //NOTE: THINKING OF USING THIS TO PLACE VALUES DIRECTLY? DONT.
    //THAT WILL BREAK THE LOGIC OF THE TEMPORAL SMOOTHING. USE setTargetVal()
    const float* getTargets()   const { return target.data(); }
    size_t size()               const { return current.size(); }

private:
    void resetAllVals() {
        std::fill(current.begin(), current.end(), minVal);
        std::fill(target.begin(), target.end(), minVal);
        std::fill(increment.begin(), increment.end(), 0.0f);
        std::fill(stepsRemaining.begin(), stepsRemaining.end(), 0);
    }

    std::vector<float> current;
    std::vector<float> target;
    std::vector<float> increment;
    std::vector<int>   stepsRemaining;

    int atkSteps = 0;
    int rlsSteps = 0;
    float minVal = 0.0f;
};

