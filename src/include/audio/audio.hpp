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

    //expects an analyze call after first true return
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
        uint32_t start;
        if (droppingFrames) {
            uint32_t write = capture.getWriteIndex();
            uint32_t rewind = globals.fftSize * capture.getNumChannels();
            uint32_t bufSize = capture.getBufferSize();
            start = (write + bufSize - rewind) % bufSize;
        }
        else {
            start = capture.getReadIndex();
        }
        float* temp = fft->getInputBuffer();
        float* buf = capture.getRawBufferPointer();
        capture.getMonoSummedWindow(temp, globals.fftSize, start);
        if (getRawSamples && isRawSamplesMono) {
            std::memcpy(rawSampleData.data(), temp + globals.fftSize - globals.hopSize,
                        globals.hopSize * sizeof(float));
        }
        else if (getRawSamples && !isRawSamplesMono) {
            int hopStart = start + (globals.fftSize - globals.hopSize)
                           * globals.numChannels;
            int trueHopSamps = globals.hopSize * globals.numChannels;
            if (hopStart + trueHopSamps > capture.getBufferSize()) {
                int firstSize = capture.getBufferSize() - hopStart;
                int secondSize = trueHopSamps - firstSize;
                std::memcpy(rawSampleData.data(),
                            buf + hopStart, firstSize * sizeof(float));
                std::memcpy(rawSampleData.data() + firstSize,
                            buf, secondSize * sizeof(float));
            }
            else {
                std::memcpy(rawSampleData.data(), buf + hopStart,
                            globals.hopSize * globals.numChannels * sizeof(float));
            }
        }
        if (isPeakRMSMono) {
            pr.getMeasurementsFromMonoSummedBlock(temp, globals.fftSize);
        }
        else {
            pr.getMeasurementsFromRingBuffer(buf, globals.fftSize, start,
                                          globals.numChannels, capture.getBufferSize());
        }
        fft->runFFT();
        if (!droppingFrames) {
            capture.setReadIndexForwardByFrames(globals.hopSize);
        }
    }
 
    void swapSpec(Spec& spec) {
        resetAccumulator();
        droppingFrames = spec.allowDroppedSamples &&
                         (globals.sampleRate > globals.displayHz * globals.hopSize);
        isPeakRMSMono = spec.isPeakRMSMono;
        pr.clear();
        pr.resize(isPeakRMSMono, globals.numChannels);
        getRawSamples = spec.getRawSamples;
        isRawSamplesMono = spec.isRawSamplesMono;
        size_t rawSampSize = isRawSamplesMono ? globals.hopSize :
                                                globals.hopSize * globals.numChannels;
        rawSampleData.resize(spec.getRawSamples ? rawSampSize : 0);
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

    float* getSamplePtr() {
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
#ifndef _WIN32
        if (!capture.playbackDevices[i].hasMonitor) {
            std::cerr << "Device " << i << " has no monitor\n";
            return false;
        }
#endif
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
    bool isRawSamplesMono = true;
    bool newDeviceOnSwap = false;
    bool droppingFrames = false;
};
