#pragma once

#include "gpu/ssbo.hpp"
#include "config/spec.hpp"
#include "bridge/av_bridge.hpp"
#include "gpu/ubo.hpp"

struct ResizeValues {
    size_t prSize = 0;
    size_t fftSize = 0;
    size_t prHSize = 0;
    size_t fftHSize = 0;
    size_t fbSize = 0;
    size_t hopSize = 0;
    float fbInit = 0.0f;
    bool getsPRHolds = false;
    bool getsFFTHolds = false;
};

struct GPUBuffers {
public:
    GPUBuffers(float fbInitVal) {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        ssbos[0].alloc(16); ssbos[0].bind(0);
        ssbos[1].alloc(16); ssbos[1].bind(1);
        ssbos[2].alloc(16); ssbos[2].bind(2);
        ssbos[3].alloc(16); ssbos[3].bind(3);
        ssbos[4].alloc(16); ssbos[4].fill(fbInitVal); ssbos[4].bind(4);
        ssbos[5].alloc(16); ssbos[5].fill(fbInitVal); ssbos[5].bind(5);
        ssbos[6].alloc(16); ssbos[6].bind(6);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    ~GPUBuffers() {
        glFinish();
        glDeleteVertexArrays(1, &vao);
    }

    void writeToBuffers(AVBridge& bridge, Spec& spec, Globals& globals) {
        //write to gpu buffers
        size_t prSize  = bridge.getPeakRMSGPUSizeInBytes();
        size_t fftSize = bridge.getFFTGPUSizeInBytes();
        size_t hopSize = bridge.getHopSizeInBytes();
        ssbos[0].write(bridge.getPeakRMSPtr(), prSize);
        ssbos[1].write(bridge.getFFTPtr(), fftSize);
        if (spec.getPeakRMSHolds) {
            ssbos[2].write(bridge.getPeakRMSHoldPtr(), prSize);
        }
        if (spec.getFFTHolds) {
            ssbos[3].write(bridge.getFFTHoldPtr(), fftSize);
        }
        if (spec.getRawSamples) {
            ssbos[6].write(bridge.getRawSamplePtr(), hopSize);
        }
        ubo.update(globals);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void swap(ResizeValues& r) {
        glFinish();
        ssbos[0].resize(r.prSize);      ssbos[0].bind(0);
        ssbos[1].resize(r.fftSize);     ssbos[1].bind(1);
        ssbos[2].resize(r.prHSize);     ssbos[2].bind(2);
        ssbos[3].resize(r.fftHSize);    ssbos[3].bind(3);
        ssbos[4].resize(r.fbSize);      ssbos[4].fill(r.fbInit);  ssbos[4].bind(4);
        ssbos[5].resize(r.fbSize);      ssbos[5].fill(r.fbInit);  ssbos[5].bind(5);
        ssbos[6].resize(r.hopSize);     ssbos[6].bind(6);
    }

    void flipFeedback() {
        feedbackFlip = !feedbackFlip;
        ssbos[4].bind(feedbackFlip ? 4 : 5);
        ssbos[5].bind(feedbackFlip ? 5 : 4);
    }

private:
    SSBO   ssbos[7];
    UBO    ubo;
    GLuint vao;
    bool   feedbackFlip = false;
};

