#pragma once

#include "audio/audio.hpp"
#include "bridge/av_bridge.hpp"

struct AudioSystem {
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << 13;
    static constexpr int fftBinAmt = fftSize / 2 + 1;
    static constexpr int hopAmt = 4;

    Audio    audio;
    AVBridge bridge;
    int      sampleRate = 0;
    int      channels = 0;

    AudioSystem(Spec& initSpec, int displayHz, int w, int h) :
                audio(fftOrder, hopAmt), bridge(audio, initSpec) {
        if (!audio.init(initSpec)) {
            std::cerr << "Audio initialization failed\n";
            return;
        }
        bridge.init(displayHz, w, h);
        sampleRate = audio.getSampleRate();
        channels = audio.getNumChannels();
    }

    bool isValid() {
        return (channels != 0 && sampleRate != 0);
    }

    bool analyzeAndFormat() {
        bool newAudioWindow = false;
        if (audio.canAnalyze()) {
            audio.analyze();
            bridge.formatData();
            newAudioWindow = true;
        }
        bridge.nextFrame();
        return newAudioWindow;
    }

    void swap(Spec& spec) {
        audio.swapSpec(spec);
        bridge.swapSpec(spec);
    }
};

