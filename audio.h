#include "audio_capture.h"
#include "analysis.h"

class Audio {
public:
    //at some point maybe add the fft customizers if the need arises
    Audio(unsigned int hops, unsigned int fft_o) : hop_amt(hops), fft_order(fft_o) {}
	~Audio() {}
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init() {
        fft_size = 1 << fft_order;
        hop_size = fft_size / hop_amt;
        const int frameAmount = fft_size * 2;

        if (capture.init(frameAmount) == false) {
            printf("Failed to initialize AudioCapture. \n");
            return false;
        }

        channels = capture.getNumChannels();
        sample_rate = capture.getSampleRate();

        std::vector<Peak> peaks(channels);
        std::vector<RMS> rmss(channels);

        fft.emplace_back(fft_size, true, false, false, true, 4.5);
        //getters for audible range here pls
        std::array<unsigned int, 2> audible = fft[0].getAudibleRange(sample_rate);
        firstAudibleIndex = audible[0];
        audibleSize = audible[1];

        return true;
    }

    void placeInContext(float* context) {
        for (int i = 0; i < channels; ++i) {
            context[i * 2] = peak[i].pop();
            context[i * 2 + 1] = rms[i].pop();
        }
        float* fftOut = fft[0].getOutputBuffer();
        int fftStart = firstAudibleIndex;
        int contextStart = channels * 2;
        for (int i = 0; i < audibleSize; ++i) {
            context[contextStart + i] = fftOut[fftStart + i];
        }
    }

    bool canAnalyze() {
        //this needs to check for accumulated samples for first run, and depending on settings for whether a full hop has been accumulated
        return false;
    }

    void analyze() {
        // --- Peak & RMS ---
        float *buff = capture.getRawBufferPointer();
        unsigned int start = capture.getWindowStartFromWrite(fft_size);
        unsigned int size = capture.getBufferSizeInSamples();
        for (int ch = 0; ch < channels; ++ch) {
            peak[ch].getPeakFromRingBuffer(buff, fft_size, ch, channels, start, size);
            rms[ch].getRMSFromRingBuffer(buff, fft_size, ch, channels, start, size);
        }

        // --- FFT ---
        capture.getMonoSummedWindow(fft[0].getInputBuffer(), fft_size);
        fft[0].runFFT();

        // --- Update Ring Buffer Indices ---
        capture.setReadIndexForwardByFrames(hop_size);
    }

private:
    AudioCapture capture;
    std::vector<RMS> rms;
    std::vector<Peak> peak;
    std::vector<FFT> fft;

    const unsigned int hop_amt;
    const unsigned int fft_order;
    unsigned int fft_size = 0;
    unsigned int hop_size = 0;

    unsigned int channels = 0;
    unsigned int sample_rate = 0;

    unsigned int firstAudibleIndex = 0;
    unsigned int audibleSize = 0;
};
