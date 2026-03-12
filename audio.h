#pragma once

#include "audio_capture.h"
#include "analysis.h"
#include "audio_spec.h"
#include <cstdint>
#include <memory>

class Audio {
public:
    //at some point maybe add the fft customizers if the need arises
    //if smoothSize is 0, it will make a smoothValue array of audible bin size
    //and will only directly place bins in array
    //if set to an output number, it will smooth based on that size and spencySmooth() 
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

        if (capture.init(frameAmount) == false) {
            printf("Failed to initialize AudioCapture. \n");
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        peak = std::make_unique<Peak[]>(channels);
        rms = std::make_unique<RMS[]>(channels);

        fft = std::make_unique<FFT>(fftSize, spec.isPerceptual, spec.isHannWindowed, spec.isDB, spec.isSingleSided, spec.slope);
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
        // --- Peak & RMS ---
        float *buff = capture.getRawBufferPointer();
        uint32_t start = capture.getWindowStartFromWrite(fftSize);
        uint32_t size = capture.getBufferSizeInSamples();
        for (int ch = 0; ch < channels; ++ch) {
            peak[ch].getPeakFromRingBuffer(buff, fftSize, ch, channels, start, size);
            rms[ch].getRMSFromRingBuffer(buff, fftSize, ch, channels, start, size);
            /*
            rmsPeakPerChannel.setTargetVal(ch * 2, rms[ch].pop());
            rmsPeakPerChannel.setTargetVal(ch * 2 + 1, peak[ch].pop());
            */
        }

        // --- FFT ---
        capture.getMonoSummedWindow(fft->getInputBuffer(), fftSize, start);
        fft->runFFT();
/*
        if (smoothFFTSize != 0) {
            spencySmooth();
        }
        else {
            binToSmooth();
        }
*/
        // --- Update Ring Buffer Indices ---
        capture.setReadIndexForwardByFrames(hopSize, start);
        capture.moveAccumulator(hopSize);
    }

    void swapSpec(AudioSpec& spec) {

    }

    void resetAccumulator() {
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

    std::vector<uint32_t> getAudibleRange() {
        return fft->getAudibleRange(sampleRate);
    }

    //if bypassing smoothedValue array, and want output buffer
    const float* getFFTPtr() {
        return fft->getOutputBuffer();
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
};
