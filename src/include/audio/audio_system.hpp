#pragma once

#include "audio/audio.hpp"
#include "bridge/av_bridge.hpp"

struct AudioSystem {
    //int fftOrder = 13;
    //int fftSize = 1 << 13;
    //int fftBinAmt = fftSize / 2 + 1;
    //int hopAmt = 4;

    Audio    audio;
    AVBridge bridge;
    int      sampleRate = 0;
    int      channels = 0;

    AudioSystem(Globals& g, Spec& initSpec) :
                globals(g), audio(g), bridge(audio, initSpec, g) {
        if (!audio.init(initSpec)) {
            std::cerr << "Audio initialization failed\n";
            return;
        }
        bridge.init();
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
private:
    Globals& globals;
};

