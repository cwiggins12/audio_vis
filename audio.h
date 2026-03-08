#include "audio_capture.h"
#include "analysis.h"
#include "smooth_value.h"
#include <cstdint>
#include <memory>

class Audio {
public:
    //at some point maybe add the fft customizers if the need arises
    Audio(uint32_t hops, uint32_t fft_o, size_t smoothSize = 0) : hopAmt(hops), fftOrder(fft_o), smoothFFTSize(smoothSize) {}
	~Audio() {}
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init(uint32_t deviceFrameRate) {
        fftSize = 1 << fftOrder;
        hopSize = fftSize / hopAmt;
        const int frameAmount = fftSize * 2;

        if (capture.init(frameAmount) == false) {
            printf("Failed to initialize AudioCapture. \n");
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        peak = std::make_unique<Peak[]>(channels);
        rms = std::make_unique<RMS[]>(channels);

        fft = std::make_unique<FFT>(fftSize, true, false, true, true, 4.5);
        fft->initFFT(sampleRate);
        //getters for audible range here pls
        std::array<unsigned int, 2> audible = fft->getAudibleRange(sampleRate);
        firstAudibleIndex = audible[0];
        audibleSize = audible[1];

        rmsPeakPerChannel.resize(channels * 2);
        rmsPeakPerChannel.reset(deviceFrameRate);
        rmsPeakPerChannel.setAsym(5, 1);

        if (smoothFFTSize == 0) {
            smoothFFT.resize(audibleSize);
        }
        else {
            smoothFFT.resize(smoothFFTSize);
            setPixelBinIndices(smoothFFTSize);
        }
        smoothFFT.reset(deviceFrameRate);
        smoothFFT.setAsym(5, 1);

        return true;
    }

    void setPixelBinIndices(size_t size) {
        pixelBinIndices.resize(size);
        bool firstHighPixelFound = false;

        const float scale = (float)fftSize / (float)sampleRate;

        for (int i = 0; i < size; ++i) {
            float normalized = (float)i / (float)(size - 1);
            float freq; //need convertFrom0to1 equivalent
            if (!firstHighPixelFound && freq > 1000.0f) {
                firstHighPixel = i;
                firstHighPixelFound = true;
            }
            float binIndexFloat = freq * scale;

            pixelBinIndices[i] = std::min(std::max(binIndexFloat, 0.0f), (float)fftSize / 2.0f + 1.0f);
        }
    }

    //expects an analyze call after first true return;
    bool canAnalyze() {
        uint64_t accumulated = capture.getAccumulatedFrames();
        if (!firstWindowAccumulated) {
            if (accumulated < fftSize) {
                return false;
            }
            firstWindowAccumulated = true;
            capture.moveAccumulator(fftSize);
            return true;
        }
        return accumulated >= hopSize;
    }

    void analyze() {
        // --- Peak & RMS ---
        float *buff = capture.getRawBufferPointer();
        unsigned int start = capture.getWindowStartFromWrite(fftSize);
        unsigned int size = capture.getBufferSizeInSamples();
        for (int ch = 0; ch < channels; ++ch) {
            peak[ch].getPeakFromRingBuffer(buff, fftSize, ch, channels, start, size);
            rms[ch].getRMSFromRingBuffer(buff, fftSize, ch, channels, start, size);
            rmsPeakPerChannel.setTargetVal(ch * 2, rms[ch].pop());
            rmsPeakPerChannel.setTargetVal(ch * 2 + 1, peak[ch].pop());
        }

        // --- FFT ---
        capture.getMonoSummedWindow(fft->getInputBuffer(), fftSize);
        fft->runFFT();

        if (smoothFFTSize == 0) {
            spencySmooth();
        }
        else {
            binToSmooth();
        }

        // --- Update Ring Buffer Indices ---
        capture.setReadIndexForwardByFrames(hopSize);

        capture.moveAccumulator(hopSize);
    }

    void spencySmooth() {
        float* fftOut = fft->getOutputBuffer();
        for (int i = 0; i < smoothFFTSize; ++i) {
            float dB;

            if (i < firstHighPixel) {
                dB = getLowFreqSmoothedValue(i, fftOut);
            }
            else {
                dB = getHighFreqSmoothedValues(i, fftOut);
            }
        smoothFFT.setTargetVal(i, dB);
        }
    }

    float getLowFreqSmoothedValue(int i, float* fftOut) {
        return 0.0f;
    }

    float getHighFreqSmoothedValues(int i, float* fftOut) {
        int lowB = (int)pixelBinIndices[i - 1] + 1;
        int highB = (int)pixelBinIndices[i];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            sumSq += fftOut[i] * fftOut[i];
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return rms; //this needs to be a decimal conversion like juce gainToDecibels
    }

/*
void SpectrumAnalyserComponent::drawNextFrameOfSpectrum() {
    auto h = (float)getHeight();
    //for each pixel, either lerp based on surrounding bins for freq < 1000 or rms each bin in pixel area for freq > 1000
    for (int i = 0; i < lastWidth; ++i) {
        float dB;

        if (i < firstHighPixel) {
            dB = getLowFreqSmoothedValue(i, pixelBinIndices[i]);
        }
        else {
            dB = getHighFreqSmoothedValues(i);
        }
        //map, clamp, then set to target (maps to y pixel pos)
        auto level = juce::jmap(dB, MIN_ANALYSIS_DB, 0.0f, h, 0.0f);
        level = juce::jlimit(0.0f, h, level);
        pixelValues[i].setTargetValue(level);
    }
}

float SpectrumAnalyserComponent::getLowFreqSmoothedValue(int pixelIndex, float centerBinFloat) {
    //use fraction for mu in interp
    int bin1 = (int)centerBinFloat;
    float fraction = centerBinFloat - bin1;

    int bin0 = juce::jmax(0, bin1 - 1);
    int bin2 = juce::jmin(FFT_BIN_AMT - 1, bin1 + 1);
    int bin3 = juce::jmin(FFT_BIN_AMT - 1, bin1 + 2);

    //set bins to dB, hate doing this repeatedly, but it is what it is
    float y0 = juce::Decibels::gainToDecibels(fftData[bin0], MIN_ANALYSIS_DB);
    float y1 = juce::Decibels::gainToDecibels(fftData[bin1], MIN_ANALYSIS_DB);
    float y2 = juce::Decibels::gainToDecibels(fftData[bin2], MIN_ANALYSIS_DB);
    float y3 = juce::Decibels::gainToDecibels(fftData[bin3], MIN_ANALYSIS_DB);

    //get 4 bins, interpolate
    return cubicInterpolate(y0, y1, y2, y3, fraction);
}

float SpectrumAnalyserComponent::getHighFreqSmoothedValues(int pixelIndex) {    
    //never overlap rms
    int lowB = (int)pixelBinIndices[pixelIndex - 1] + 1;
    int highB = (int)pixelBinIndices[pixelIndex];

    //get root mean squared
    float sumSq = 0.0f;
    for (int i = lowB; i <= highB && i < FFT_BIN_AMT; ++i) {
        sumSq += fftData[i] * fftData[i];
    }
    float rms = std::sqrt(sumSq / (highB - lowB + 1));
    //return as dB
    return juce::Decibels::gainToDecibels(rms, MIN_ANALYSIS_DB);
}
*/

    void binToSmooth() {
        float* fftPtr = fft->getOutputBuffer();
        float* smooth = smoothFFT.getTargets();
        for (int i = 0; i < audibleSize; ++i) {
            smooth[i] = fftPtr[i];
        }
    }

    unsigned int getNumChannels() {
        return channels;
    }

    unsigned int getFirstAudibleIndex() {
        return firstAudibleIndex;
    }

    unsigned int getAudibleSize() {
        return audibleSize;
    }

    const float* getFFTPtr() {
        return fft->getOutputBuffer() + firstAudibleIndex;
    }

    const float* getSmoothFFTPtr() {
        return smoothFFT.getCurrents();
    }

    const float* getRMSPeakPtr() {
        return rmsPeakPerChannel.getCurrents();
    }

    void resize(size_t newSize) {
        setPixelBinIndices(newSize);
    }

private:
    AudioCapture capture;
    std::unique_ptr<RMS[]> rms;
    std::unique_ptr<Peak[]> peak;
    std::unique_ptr<FFT> fft;

    SmoothArraySoA rmsPeakPerChannel;
    SmoothArraySoA smoothFFT;
    std::vector<float> pixelBinIndices;

    const uint32_t hopAmt;
    const uint32_t fftOrder;

    bool firstWindowAccumulated = false;

    uint32_t fftSize = 0;
    uint32_t hopSize = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    uint32_t firstAudibleIndex = 0;
    uint32_t audibleSize = 0;
    size_t smoothFFTSize;
    uint32_t firstHighPixel = 0;
};
