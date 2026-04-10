#pragma once

#include <cstring>

//the plan: make everything inherit from this struct, and only use this as the source of truth for these vars
//this struct will directly correllate to the ubo that is sent. We will need constants of size of this struct.
struct Globals {
    float bpm = 0.0f;    //need sys in audio
    int mouseX = 0; //input handler
    int mouseY = 0; //input handler
    int windowX = 0; //glfw context DONE
    int windowY = 0; //glfw context DONE
    int time = 0; //in main DONE
    int W = 0; //glfw context DONE
    int H = 0; //glfw context DONE
    int mouseDown = 0; //input handler

    int fftSize = 0;   // handle these three in audioSys
    int hopSize = 0;
    int fftBinAmt = 0;

    int fftArrSize = 0; //handle within avbridge (could move some of the resize functions to this struct)

    int newAudioWindow = 0; //in main DONE
    int numChannels = 0; //get from capture DONE
    int displayHz = 0; //glfw context DONE
    int sampleRate = 0; //get from capture DONE

    int showError = 0; //set these 3 in shader system swap
    int errorLen = 0;
    int errorChars[128];
};

