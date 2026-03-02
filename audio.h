#include "audio_capture.h"
#include "analysis.h"
#include <memory>

class Audio {
public:
    //at some point maybe add the fft customizers if the need arises
    Audio(unsigned int hops, unsigned int fft_o) : hopAmt(hops), fftOrder(fft_o) {}
	~Audio() {}
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init() {
        fftSize = 1 << fftOrder;
        hopSize = fftSize / hopAmt;
        const int frameAmount = fftSize * 2;

        if (capture.init(frameAmount) == false) {
            printf("Failed to initialize AudioCapture. \n");
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        peak = std::make_unique<Peak[]>(channels);
        rms = std::make_unique<RMS[]>(channels);

        fft = std::make_unique<FFT>(fftSize, true, false, false, true, 4.5);
        fft->initFFT(sampleRate);
        //getters for audible range here pls
        std::array<unsigned int, 2> audible = fft->getAudibleRange(sampleRate);
        firstAudibleIndex = audible[0];
        audibleSize = audible[1];

        rmsPeakPerChannel.resize(channels * 2);

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
        unsigned int start = capture.getWindowStartFromWrite(fftSize);
        unsigned int size = capture.getBufferSizeInSamples();
        for (int ch = 0; ch < channels; ++ch) {
            peak[ch].getPeakFromRingBuffer(buff, fftSize, ch, channels, start, size);
            rms[ch].getRMSFromRingBuffer(buff, fftSize, ch, channels, start, size);
            rmsPeakPerChannel[ch * 2] = rms[ch].pop();
            rmsPeakPerChannel[ch * 2 + 1] = peak[ch].pop();
        }

        // --- FFT ---
        capture.getMonoSummedWindow(fft->getInputBuffer(), fftSize);
        fft->runFFT();

        // --- Update Ring Buffer Indices ---
        capture.setReadIndexForwardByFrames(hopSize);

        capture.moveAccumulator(hopSize);
    }

    unsigned int getNumChannels() {
        return channels;
    }

    unsigned int getFirstAudibleIndex() {
        return firstAudibleIndex;
    }

    unsigned int getAudibleSize() {
        return audibleSize;
    }

    const float* getFFTPtr() {
        return fft->getOutputBuffer() + firstAudibleIndex;
    }

    const float* getRMSPeakPtr() {
        return rmsPeakPerChannel.data();
    }

private:
    AudioCapture capture;
    std::unique_ptr<RMS[]> rms;
    std::unique_ptr<Peak[]> peak;
    std::unique_ptr<FFT> fft;

    std::vector<float> rmsPeakPerChannel;
    const uint32_t hopAmt;
    const uint32_t fftOrder;

    bool firstWindowAccumulated = false;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    uint32_t firstAudibleIndex = 0;
    uint32_t audibleSize = 0;
};
