#pragma once

#include "audio_capture.h"
#include "peak_rms.h"
#include "fft.h"
#include "audio_spec.h"
#include <cstdint>
#include <memory>
#include <iostream>

class Audio {
public:
    Audio(uint32_t fft_o) : fftOrder(fft_o) {}
	~Audio() {}
    //no moves, no copies
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init(AudioSpec& spec) {
        fftSize = 1 << fftOrder;
        hopSize = fftSize / spec.hopAmt;
        const int frameAmount = fftSize * 2;

        if (!capture.init(frameAmount)) {
            std::cerr << "Failed to initialize AudioCapture." << std::endl;
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        peak = std::make_unique<Peak[]>(channels);
        rms = std::make_unique<RMS[]>(channels);
        fft = std::make_unique<FFT>(fftSize, spec.isPerceptual, spec.isHannWindowed, 
                                    spec.isFFTdB, true, spec.perceptualSlopeDegrees);
        fft->initFFT(sampleRate);

        return true;
    }

    //expects an analyze call after first true return;
    bool canAnalyze() {
        uint64_t accumulated = capture.getAccumulatedFrames();
        if (!firstWindowAccumulated) {
            if (accumulated < fftSize) {
                return false;
            }
            firstWindowAccumulated = true;
            capture.moveAccumulator(fftSize);
            return true;
        }
        return accumulated >= hopSize;
    }

    void analyze() {
        uint32_t start = capture.getWindowStartFromWrite(fftSize);
        capture.getMonoSummedWindow(fft->getInputBuffer(), fftSize, start);

        if (isPeakRMSMono) {
            float* buf = fft->getInputBuffer();
            peak[0].getPeakFromMonoSummedBlock(buf, fftSize);
            rms[0].getRMSFromMonoSummedBlock(buf, fftSize);
        }
        else {
            float *buf = capture.getRawBufferPointer();
            uint32_t size = capture.getBufferSizeInSamples();
            for (int ch = 0; ch < channels; ++ch) {
                peak[ch].getPeakFromRingBuffer(buf, fftSize, ch, channels, start, size);
                rms[ch].getRMSFromRingBuffer(buf, fftSize, ch, channels, start, size);
            }
        }

        fft->runFFT();

        capture.setReadIndexForwardByFrames(hopSize, start);
        capture.moveAccumulator(hopSize);
    }

    void swapSpec(AudioSpec& spec) {
        capture.resetAccumulator();
        for (int ch = 0; ch < channels; ++ch) {
            popPeak(ch);
            popRMS(ch);
        }
        isPeakRMSMono = spec.isPeakRMSMono;

        //set this way to account for arb sized array being more efficient to
        //just get db the convert after sizing
        bool db = (spec.customLinearSize != 0 && !spec.useAudibleSize) 
                   ? true : spec.isFFTdB;
        fft->swapSpec(spec.isPerceptual, spec.isHannWindowed, db,
                      spec.perceptualSlopeDegrees, sampleRate);
    }

    void resetAccumulator() {
        capture.resetAccumulator();
        firstWindowAccumulated = false;
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
        fft->getAudibleRange(sampleRate, start, size);
    }

    //if bypassing smoothedValue array, and want output buffer
    const float* getFFTPtr() {
        return fft->getOutputBuffer();
    }

    float popPeak(int ch) {
        return peak[ch].pop();
    }

    float popRMS(int ch) {
        return rms[ch].pop();
    }

private:
    AudioCapture capture;

    std::unique_ptr<RMS[]> rms;
    std::unique_ptr<Peak[]> peak;
    std::unique_ptr<FFT> fft;

    const uint32_t fftOrder;

    bool firstWindowAccumulated = false;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    //spec given/current values
    //fft configs
    bool isPerceptual = true;
    bool isHannWindowed = true;
    bool isDB = true;
    bool isSingleSided = true;
    float slope = 4.5;
    bool getsFFTHolds = true;
    //peak/rms configs
    bool getsPeakRMSHolds = true;
    bool isPeakRMSMono = false;
};
