#pragma once

#include "audio/audio.hpp"
#include "bridge/av_bridge.hpp"

struct AudioSystem {
    Audio    audio;
    AVBridge bridge;

    AudioSystem(Globals& g, Spec& spec) :
                globals(g), audio(g), bridge(audio, spec, g) {
        globals.fftSize = 1 << spec.fftOrder;
        globals.hopSize = globals.fftSize / spec.hopAmount;
        globals.fftBinAmt = globals.fftSize / 2 + 1;
        if (!audio.init(spec)) {
            std::cerr << "Audio initialization failed\n";
            return;
        }
        bridge.init();
    }

    bool isValid() {
        return (globals.numChannels != 0 && globals.sampleRate != 0);
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
        globals.fftSize = 1 << spec.fftOrder;
        globals.hopSize = globals.fftSize / spec.hopAmount;
        globals.fftBinAmt = globals.fftSize / 2 + 1;
        audio.swapSpec(spec);
        bridge.swapSpec(spec);
    }
private:
    Globals& globals;
};

