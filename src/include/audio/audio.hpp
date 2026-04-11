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
            std::memcpy(rawSampleData.data(), temp + globals.fftSize - globals.hopSize, globals.hopSize);
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
        fft->swapSpec(spec, globals.sampleRate);
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

private:
    AudioCapture capture;

    PeakRMSMeter pr;
    std::unique_ptr<FFT> fft;

    Globals& globals;

    std::vector<float> rawSampleData;

    bool firstWindowAccumulated = false;
    bool isPeakRMSMono = false;
    bool getRawSamples = true;
};

