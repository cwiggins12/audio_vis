#define MINIAUDIO_IMPLEMENTATION

#include "audio_capture.h"
#include "analysis.h"
#include <csignal>
#include <thread>
#include <chrono>

static volatile bool running = true;

void handleSignal(int) {
    running = false;
}

int main() {
    std::signal(SIGINT, handleSignal);

    const int hop_amt  = 4;
    const int fft_order = 12;
    const int fft_size = 1 << fft_order;
    const int hop_size = fft_size / hop_amt;
    const int frameAmount = fft_size * 2;

    AudioCapture capture;
    if (capture.init(frameAmount) == false) {
        printf("Failed to initialize AudioCapture. Exiting \n");
        return -1;
    }

    const unsigned int channels = capture.getNumChannels();
    const unsigned int sampleRate = capture.getSampleRate();

    std::vector<Peak> peaks(channels);
    std::vector<RMS> rmss(channels);

    FFT fft(fft_size, true, true, true, true, sampleRate, 4.5f);

    const float binWidth = (float)sampleRate / (float)fft_size;
    const int binCount = fft_size / 2 + 1;

    printf("\nStarted. Press Ctrl+C to stop.\n\n");

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (!capture.getFirstFillFlag()) {
            continue;
        }

        // --- Peak & RMS ---
        float *buff = capture.getRawBufferPointer();
        unsigned int start = capture.getWindowStartFromWrite(fft_size);
        unsigned int size = capture.getBufferSizeInSamples();
        for (int ch = 0; ch < channels; ++ch) {
            peaks[ch].getPeakFromRingBuffer(buff, fft_size, ch, channels, start, size);
            rmss[ch].getRMSFromRingBuffer(buff, fft_size, ch, channels, start, size);
        }

        // --- FFT ---
        capture.getMonoSummedWindow(fft.getInputBuffer(), fft_size);
        fft.runFFT();
        float* fftOut = fft.getOutputBuffer();

        capture.setReadIndexForwardByFrames(hop_size);

        //printouts
        printf("=== Peak & RMS ===\n");
        for (int ch = 0; ch < channels; ++ch) {
            float peakDb = 20.0f * std::log10(std::max(peaks[ch].pop(), 1e-12f));
            float rmsDb  = 20.0f * std::log10(std::max(rmss[ch].pop(),  1e-12f));
            printf("  Ch%d  Peak: %6.2f dBFS   RMS: %6.2f dBFS\n", ch, peakDb, rmsDb);
        }

        printf("\n=== FFT Bins ===\n");
        printf("  %6s  %10s  %8s\n", "Bin", "Freq (Hz)", "dB");
        printf("  %6s  %10s  %8s\n", "------", "----------", "--------");
        for (int i = 0; i < binCount; ++i) {
            printf("  %6d  %10.2f  %8.2f\n", i, (float)i * binWidth, fftOut[i]);
        }
    }
    printf("Stopped. :)\n");
    return 0;
}
