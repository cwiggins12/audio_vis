#include "audio_capture.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

int main()
{
    AudioCapture capture;

    const ma_uint32 framesPerBuffer = 512;

    if (!capture.init(framesPerBuffer))
    {
        std::cerr << "Failed to initialize audio capture.\n";
        return 1;
    }

    std::cout << "Capturing for 1 second...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Decide how many FRAMES you want
    const ma_uint32 framesToRead = framesPerBuffer * 4;

    // Assume stereo for now
    const ma_uint32 channels = 2;

    // Allocate enough floats: frames * channels
    std::vector<float> snapshot(framesToRead * channels);

    // Pass frame count only
    ma_uint32 lastWriteIndex = capture.getBlock(snapshot.data(), framesToRead);

    std::cout << "Last write index: " << lastWriteIndex << "\n";

    std::cout << "First 10 samples:\n";
    for (ma_uint32 i = 0; i < std::min((ma_uint32)snapshot.size(), (ma_uint32)10); ++i)
    {
        std::cout << snapshot[i] << "\n";
    }

    return 0;
}
