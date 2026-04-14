#pragma once

#include "audio/audio_capture.hpp"
#include "audio/peak_rms.hpp"
#include "audio/fft.hpp"
#include "config/spec.hpp"
#include "config/globals.hpp"
#include <cstdint>
#include <memory>
#include <iostream>

class Audio {
public:
    Audio(Globals& g) : globals(g) {}
	~Audio() {}
    //no moves, no copies
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init(Spec& spec) {
        const int frameAmount = MAX_FFT_SIZE * 2;

        if (!capture.init(frameAmount)) {
            std::cerr << "Failed to initialize AudioCapture." << std::endl;
            return false;
        }

        globals.numChannels = capture.getNumChannels();
        globals.sampleRate = capture.getSampleRate();

        pr.resize(spec.isPeakRMSMono, globals.numChannels);
        fft = std::make_unique<FFT>(globals.fftSize, spec.perceptualSlopeDegrees != 0.0f,
                                    spec.isFFTHannWindowed, spec.fftOutputMeasurement,
                                    true, spec.perceptualSlopeDegrees);
        fft->initFFT(globals.sampleRate);
        rawSampleData.resize(spec.getRawSamples ? globals.hopSize : 0);
        return true;
    }

    //expects an analyze call after first true return;
    bool canAnalyze() {
        uint32_t accumulated = capture.getAccumulatedFrames();
        if (!firstWindowAccumulated) {
            if (accumulated < globals.fftSize) {
                return false;
            }
            firstWindowAccumulated = true;
            capture.moveAccumulator(globals.fftSize);
            return true;
        }
        if (accumulated >= globals.hopSize) {
            capture.moveAccumulator(globals.hopSize);
            return true;
        }
        return false;
    }

    void analyze() {
        uint32_t start = capture.getReadIndex();
        float* temp = fft->getInputBuffer();
        capture.getMonoSummedWindow(temp, globals.fftSize, start);
        if (getRawSamples) {
            std::memcpy(rawSampleData.data(), temp + globals.fftSize - globals.hopSize,
                        globals.hopSize * sizeof(float));
        }
        if (isPeakRMSMono) {
            pr.getMeasurementsFromMonoSummedBlock(temp, globals.fftSize);
        }
        else {
            float* buf = capture.getRawBufferPointer();
            pr.getMeasurementsFromRingBuffer(buf, globals.fftSize, start,
                                             globals.numChannels, capture.getBufferSize());
        }
        fft->runFFT();
        capture.setReadIndexForwardByFrames(globals.hopSize);
    }

    void swapSpec(Spec& spec) {
        resetAccumulator();
        isPeakRMSMono = spec.isPeakRMSMono;
        pr.clear();
        pr.resize(isPeakRMSMono, globals.numChannels);
        getRawSamples = spec.getRawSamples;
        rawSampleData.resize(spec.getRawSamples ? globals.hopSize : 0);
        fft->swapSpec(spec, globals.sampleRate, newDeviceOnSwap);
        newDeviceOnSwap = false;
    }

    void resetAccumulator() {
        firstWindowAccumulated = false;
        capture.resetAccumulator();
    }

    void getAudibleRange(int* start, int* size) {
        fft->getAudibleRange(capture.getSampleRate(), start, size);
    }

    const float* getFFTPtr() {
        return fft->getOutputBuffer();
    }

    float* getPRPtr() {
        return pr.getPtr();
    }

    const float* getSamplePtr() {
        return rawSampleData.data();
    }

    void prClear() {
        pr.clear();
    }

    void updateDeviceGlobals() {
        capture.enumerateDevices();
        std::string list = capture.formatDeviceList();
        globals.deviceChars = formatDeviceMenuMessage(list, globals.deviceMenuLen);
    }
 
    bool reconfigureToDeviceAtIndex(int i) {
        if (i >= capture.getDeviceCount()) {
            std::cerr << "Device index " << i << " out of range\n";
            return false;
        }
        if (!capture.playbackDevices[i].hasMonitor) {
            std::cerr << "Device " << i << " has no monitor\n";
            return false;
        }
        if (i == capture.getCurrentDeviceIndex()) {
            std::cout << "Already on device " << i << "\n";
            return false;
        }
        if (!capture.switchToDevice(i)) {
            std::cerr << "Failed to switch to device " << i << "\n";
            return false;
        }
        // update globals with new device's properties
        globals.numChannels = capture.getNumChannels();
        globals.sampleRate = capture.getSampleRate();
        newDeviceOnSwap = true;
        return true;
    }

    //void updateDeviceGlobals() {
        //need to call a device enumeration and get it returned here, then format it in the below helper function
        //need to update the deviceChars and deviceTextLen vars here with that function based on what audioCapture returns

    //}

    //void reconfigureToDeviceAtIndex(int i) {
        //this will return a device at the enumerated position. This logic may need to be more complex if reenumerating the devices
        //may result in different ordering. If this is being called, it is guaranteed that swap will be called soon after
        //So, no swap functions are necessary here, this just sets up the capture with the new device and shutsdown the prior device.
        //newDeviceOnSwap = true;
    //}

    std::array<int, 512> formatDeviceMenuMessage(const std::string& msg, int& len) {
        len = std::min((int)msg.size(), 512);
        std::array<int, 512> ret;
        for (int i = 0; i < len; ++i) {
            ret[i] = (int)msg[i];
        }
        return ret;
    }

private:
    AudioCapture capture;
    PeakRMSMeter pr;
    std::unique_ptr<FFT> fft;
    Globals& globals;
    std::vector<float> rawSampleData;
    bool firstWindowAccumulated = false;
    bool isPeakRMSMono = false;
    bool getRawSamples = true;
    bool newDeviceOnSwap = false;
};

