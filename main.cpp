#include "audio_capture.h"
#include <iostream>
#include <thread>

int main() {
    AudioCapture audio(60);

    if (!audio.init()) {
        std::cerr << "Failed to initialize audio capture\n";
        return 1;
    }

    std::cout << "Capturing audio... Press Ctrl+c to quit. \n";

    while (true) {
        std::vector<float> samples;
        audio.getSamples(samples);

        std::cout<< "Sample[0]: " << samples[0] << "\r";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
}
