#define MINIAUDIO_IMPLEMENTATION

#include "audio_capture.h"
#include "analysis.h"

int main() {
    AudioCapture capture;

    const ma_uint32 framesPerBuffer = 512;
    const ma_uint32 fft_size = 2048;

    if (!capture.init(framesPerBuffer)) {
        std::cerr << "Failed to initialize audio capture.\n";
        return 1;
    }

    const ma_uint32 channels = capture.getChannels();
    
    std::vector<Peak> peaks(channels);
    std::vector<RMS> rmss(channels);
    //NOTE: remember to mono sum BEFORE PUSHING TO THIS OBJECT
    FFT fft(fft_size);



    return 0;
}
