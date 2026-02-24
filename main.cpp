#define MINIAUDIO_IMPLEMENTATION

#include "audio_capture.h"
#include "analysis.h"

int main() {
    AudioCapture capture;
    
    const int fft_size = 2048;
    const int channels = capture.getChannels();
    
    std::vector<Peak> peaks(channels);
    std::vector<RMS> rmss(channels);
    //NOTE: remember to mono sum BEFORE PUSHING TO THIS OBJECT
    FFT fft(fft_size);



    return 0;
}
