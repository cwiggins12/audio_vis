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
    const int fft_size = 1024;
    const int hop_size = fft_size / hop_amt;
    const int frameAmount = fft_size + hop_size;

    AudioCapture capture;
    if (capture.init(frameAmount) == false) {
        printf("Failed to initialize AudioCapture. Exiting \n");
        return -1;
    }

    const int channels = capture.getNumChannels();
    const float sampleRate = (float)capture.getSampleRate();

    std::vector<Peak> peaks(channels);
    std::vector<RMS> rmss(channels);

    FFT fft(fft_size, true, true, true, sampleRate, 4.5f);

    std::vector<float> monoWindow(fft_size);
    std::vector<float> interleavedWindow(fft_size * channels);

    const float binWidth = sampleRate / (float)fft_size;
    const int binCount = fft_size / 2 + 1;

    FILE* csvFile = fopen("fft_log.csv", "w");
    if (!csvFile) {
        printf("Failed to open log file\n");
        return -1;
    }

    fprintf(csvFile, "timestamp_ms");
    for (int i = 0; i < binCount; ++i) {
        fprintf(csvFile, ",bin%d_%.2fHz", i, (float)i * binWidth);
    }
    fprintf(csvFile, "\n");

    printf("\nStarted. Press Ctrl+C to stop.\n\n");

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // --- Peak & RMS ---
        capture.getWindow(interleavedWindow.data(), fft_size);

        for (int ch = 0; ch < channels; ++ch) {
            peaks[ch].getPeakFromBlock(interleavedWindow.data(), fft_size, ch, channels);
            rmss[ch].getRMSFromBlock(interleavedWindow.data(), fft_size, ch, channels);
        }

        printf("=== Peak & RMS ===\n");
        for (int ch = 0; ch < channels; ++ch) {
            float peakDb = 20.0f * std::log10(std::max(peaks[ch].pop(), 1e-12f));
            float rmsDb  = 20.0f * std::log10(std::max(rmss[ch].pop(),  1e-12f));
            printf("  Ch%d  Peak: %6.2f dBFS   RMS: %6.2f dBFS\n", ch, peakDb, rmsDb);
        }

        // --- FFT ---
        capture.getMonoSummedWindow(monoWindow.data(), fft_size);
        std::memcpy(fft.getInputBuffer(), monoWindow.data(), sizeof(float) * fft_size);
        fft.runFFT();

        float* fftOut = fft.getOutputBuffer();

        // print to terminal
        printf("\n=== FFT Bins ===\n");
        printf("  %6s  %10s  %8s\n", "Bin", "Freq (Hz)", "dB");
        printf("  %6s  %10s  %8s\n", "------", "----------", "--------");
        for (int i = 0; i < binCount; ++i) {
            printf("  %6d  %10.2f  %8.2f\n", i, (float)i * binWidth, fftOut[i]);
        }

        // write to file
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        fprintf(csvFile, "%lld", (long long)ms);
        for (int i = 0; i < binCount; ++i) {
            fprintf(csvFile, ",%.2f", fftOut[i]);
        }
        fprintf(csvFile, "\n");
        fflush(csvFile);

        printf("\n");
    }

    fclose(csvFile);
    printf("Stopped. Data written to fft_log.csv\n");
    return 0;
}
