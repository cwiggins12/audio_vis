#include "audio_capture.h"
#include "analysis.h"

int main() {
    const int hop_amt = 4;
    const int fft_size = 2048;
    const int hop_size = fft_size / hop_amt;
    int frameAmount = fft_size + hop_size;
    AudioCapture capture(frameAmount);

    const int channels = capture.getChannels();

    std::vector<Peak> peaks(channels);
    std::vector<RMS> rmss(channels);
    //NOTE: remember to mono sum BEFORE PUSHING TO THIS OBJECT
    FFT fft(fft_size);



    return 0;
}
