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
               float rlsSecs, size_t newSize, float min = 0.0f) {
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

    //fix pls
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

    void setAllTargets(float val) {
        for (size_t i = 0; i < current.size(); i++) {
            setTargetVal(i, val);
        }
    }

    void setAllCurrentAndTargets(float val) {
        for (size_t i = 0; i < current.size(); i++) {
            setCurrentAndTargetVal(i, val);
        }
    }

    //getters

    const float* getCurrents()  const { return current.data(); }
    float* getTargets()               { return target.data(); }
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


//based on juce LinearSmoothedValue, be careful on multi thread use.
//best practice is to have 1 thread set targets, and another thread get currents
//don't use this for frequency smoothing and make sure to call setup before use
struct LinearSmoothVal {
    float getCurrentVal() {
        return current;
    }

    float getTargetVal() {
        return target;
    }

    bool isSmoothing() {
        return steps_remaining > 0;
    }

    void setCurrentAndTargetVal(float val) {
        current = val;
        target = val;
        steps_remaining = 0;
        increment = 0.0f;
    }

    void setCurrentToTargetVal() {
        current = target;
        steps_remaining = 0;
        increment = 0.0f;
    }
    //expects pass of max_steps kept in parent array
    void setTargetVal(float val, int steps) {
        if (target == val) {
            return;
        }
        if (steps <= 0) {
            setCurrentAndTargetVal(val);
            return;
        }
        target = val;
        steps_remaining = steps;
        increment = (target - current) / steps_remaining;
    }

    float getNextVal() {
        if (!isSmoothing()) {
            return target;
        }
        --steps_remaining;
        if (isSmoothing()) {
            current += increment;
        }
        else {
            current = target;
        }
        return current;
    }

    float getSkipVal(uint32_t amtToSkip) {
        if (amtToSkip >= steps_remaining) {
            setCurrentAndTargetVal(target);
            return target;
        }
        current += increment * (float) amtToSkip;
        steps_remaining -= amtToSkip;
        return current;
    }

private:
    float target = 0.0f;
    float current = 0.0f;
    float increment = 0.0f;
    int steps_remaining = 0;
};

//need to refactor this to match the logic of the SoA version
//this version is still here for reference or use on another project, but
//due to my specific use case here, its not being used
//array of LinearSmoothValues, but with a shared setup. 
//16 bytes per value rather than JUCE's 20
//[i] is overridden to allow for any LinearSmoothValue 
//call to work with the internal vector.
//I also set a custom call of getAsymVal(int index) 
//in the common case of asymmetric attack/release
//that asym call is slower if you are not adjusting 
//the atk/rls values, so only use it in the case of when you adjust them
struct SmoothArray {
    //left for use as a member variable, but make sure to 
    //run resize(), setup(), and setAsym() ASAP or all hell breaks loose
    SmoothArray() = default;

    SmoothArray(uint32_t sampleRate, float rampLengthInSeconds, size_t size, 
                uint32_t atk = 1, uint32_t rls = 1) {
        arr.resize(size);
        //if you pass a 0 or less as either of these, you're gonna have a bad time
        setup((int)std::floor(rampLengthInSeconds * sampleRate));
        setAsym(atk, rls);
    }

    SmoothArray(int steps, size_t size, uint32_t atk = 1, uint32_t rls = 1) {
        arr.resize(size);
        setup(steps);
        setAsym(atk, rls);
    }

    // Copy constructor
    SmoothArray(const SmoothArray& other) : arr(other.arr), max_steps(other.max_steps), 
                attack_steps(other.attack_steps), release_steps(other.release_steps) {}

    // Copy assignment
    SmoothArray& operator=(const SmoothArray& other) {
        if (this != &other) {
            arr = other.arr;
            max_steps = other.max_steps;
            attack_steps = other.attack_steps;
            release_steps = other.release_steps;
        }
        return *this;
    }

    // Move constructor
    SmoothArray(SmoothArray&& other) noexcept : arr(std::move(other.arr)), 
                max_steps(other.max_steps), attack_steps(other.attack_steps), 
                release_steps(other.release_steps) {}

    // Move assignment
    SmoothArray& operator=(SmoothArray&& other) noexcept {
        if (this != &other) {
            arr = std::move(other.arr);
            max_steps = other.max_steps;
            attack_steps = other.attack_steps;
            release_steps = other.release_steps;
        }
        return *this;
    }

    ~SmoothArray() = default;

    //direct setting max_steps
    void setup(int steps) {
        max_steps = steps;
        for (auto &val : arr) {
            val.setCurrentToTargetVal();
        }
    }

    void setAsym(uint32_t up, uint32_t down) {
        attack_steps = up;
        release_steps = down;
    }

    void setTargetVal(size_t i, float val) {
        arr[i].setTargetVal(val, max_steps);
    }

    float getAsymVal(size_t i) {
        if (arr[i].getCurrentVal() < arr[i].getTargetVal()) {
            return arr[i].getSkipVal(attack_steps);
        }
        else {
            return arr[i].getSkipVal(release_steps);
        }
    }

    size_t size() const { return arr.size(); }

    LinearSmoothVal& operator[](size_t i) { return arr[i]; }
    const LinearSmoothVal& operator[](size_t i) const { return arr[i]; }

    auto begin()        { return arr.begin(); }
    auto end()          { return arr.end(); }
    auto begin() const  { return arr.begin(); }
    auto end() const    { return arr.end(); }

private:
    std::vector<LinearSmoothVal> arr;
    int max_steps = 0;
    uint32_t attack_steps = 1;
    uint32_t release_steps = 1;
};

