#pragma once

#include "audio/audio_capture.hpp"
#include "audio/peak_rms.hpp"
#include "audio/fft.hpp"
#include "config/spec.hpp"
#include <cstdint>
#include <memory>
#include <iostream>

class Audio {
public:
    Audio(uint32_t fft_o, uint32_t hops) : fftOrder(fft_o), hopAmt(hops) {}
	~Audio() {}
    //no moves, no copies
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init(Spec& spec) {
        fftSize = 1 << fftOrder;
        hopSize = fftSize / hopAmt;
        const int frameAmount = fftSize * 2;

        if (!capture.init(frameAmount)) {
            std::cerr << "Failed to initialize AudioCapture." << std::endl;
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        pr.resize(spec.isPeakRMSMono, channels);
        fft = std::make_unique<FFT>(fftSize, spec.perceptualSlopeDegrees != 0.0f,
                                    spec.isFFTHannWindowed, spec.fftOutputMeasurement,
                                    true, spec.perceptualSlopeDegrees);
        fft->initFFT(sampleRate);

        return true;
    }

    //expects an analyze call after first true return;
    bool canAnalyze() {
        uint32_t accumulated = capture.getAccumulatedFrames();
        if (!firstWindowAccumulated) {
            if (accumulated < fftSize) {
                return false;
            }
            firstWindowAccumulated = true;
            capture.moveAccumulator(fftSize);
            return true;
        }
        if (accumulated >= hopSize) {
            capture.moveAccumulator(hopSize);
            return true;
        }
        return false;
    }

    void analyze() {
        uint32_t start = capture.getReadIndex();
        capture.getMonoSummedWindow(fft->getInputBuffer(), fftSize, start);

        if (isPeakRMSMono) {
            float* buf = fft->getInputBuffer();
            pr.getMeasurementsFromMonoSummedBlock(buf, fftSize);
        }
        else {
            float* buf = capture.getRawBufferPointer();
            pr.getMeasurementsFromRingBuffer(buf, fftSize, start, channels);
        }
        fft->runFFT();
        capture.setReadIndexForwardByFrames(hopSize);
    }

    void swapSpec(Spec& spec) {
        resetAccumulator();
        isPeakRMSMono = spec.isPeakRMSMono;
        pr.clear();
        pr.resize(isPeakRMSMono, channels);
        //set this way to account for custom sized array being more efficient to
        //just get db then convert after all the ops it does
        FFTMeasurement m = (spec.fftOutputMode == CUSTOM_SIZE) ? DECIBELS :
                                                              spec.fftOutputMeasurement;
        fft->swapSpec(spec.isFFTHannWindowed, m,
                      spec.perceptualSlopeDegrees, sampleRate);
    }

    void resetAccumulator() {
        firstWindowAccumulated = false;
        capture.resetAccumulator();
    }

    uint32_t getNumChannels() {
        return channels;
    }

    uint32_t getSampleRate() {
        return sampleRate;
    }

    uint32_t getFFTSize () {
        return fftSize;
    }

    void getAudibleRange(uint32_t* start, uint32_t* size) {
        fft->getAudibleRange(capture.getSampleRate(), start, size);
    }

    const float* getFFTPtr() {
        return fft->getOutputBuffer();
    }

    float* getPRPtr() {
        return pr.getPtr();
    }

    void prClear() {
        pr.clear();
    }

private:
    AudioCapture capture;

    PeakRMSMeter pr;
    std::unique_ptr<FFT> fft;

    const uint32_t fftOrder;
    const uint32_t hopAmt;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    bool firstWindowAccumulated = false;
    bool isPeakRMSMono = false;
};

