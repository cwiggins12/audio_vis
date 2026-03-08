#include "audio_capture.h"
#include "analysis.h"
#include "smooth_value.h"
#include <cstdint>
#include <memory>

static constexpr float MIN_FREQ = 20.0f;
static constexpr float MID_FREQ = 1000.0f;
static constexpr float MAX_FREQ = 20000.0f;
static constexpr float MIN_DB = -96.0f;

static constexpr int PEAK_RMS_ATK = 5;
static constexpr int PEAK_RMS_RLS = 1;
static constexpr int FFT_ATK = 5;
static constexpr int FFT_RLS = 1;

class Audio {
public:
    //at some point maybe add the fft customizers if the need arises
    //if smoothSize is 0, it will make a smoothValue array of audible bin size
    //and will only directly place bins in array
    //if set to an output number, it will smooth based on that size and spencySmooth() 
    Audio(uint32_t hops, uint32_t fft_o, size_t smoothSize = 0) : 
          hopAmt(hops), fftOrder(fft_o), smoothFFTSize(smoothSize) {}
	~Audio() {}
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	Audio(Audio&&) = delete;
	Audio& operator=(Audio&&) = delete;

    bool init(uint32_t deviceFrameRate) {
        fftSize = 1 << fftOrder;
        hopSize = fftSize / hopAmt;
        totalBinAmt = fftSize / 2 + 1;
        const int frameAmount = fftSize * 2;

        if (capture.init(frameAmount) == false) {
            printf("Failed to initialize AudioCapture. \n");
            return false;
        }

        channels = capture.getNumChannels();
        sampleRate = capture.getSampleRate();

        peak = std::make_unique<Peak[]>(channels);
        rms = std::make_unique<RMS[]>(channels);

        fft = std::make_unique<FFT>(fftSize, true, true, true, true, 4.5);
        fft->initFFT(sampleRate);
        //getters for audible range here pls
        std::array<unsigned int, 2> audible = fft->getAudibleRange(sampleRate);
        firstAudibleIndex = audible[0];
        audibleSize = audible[1];

        rmsPeakPerChannel.resize(channels * 2);
        rmsPeakPerChannel.reset(deviceFrameRate);
        rmsPeakPerChannel.setAsym(PEAK_RMS_ATK, PEAK_RMS_RLS);

        if (smoothFFTSize == 0) {
            smoothFFT.resize(audibleSize);
        }
        else {
            smoothFFT.resize(smoothFFTSize);
            setPixelBinIndices(smoothFFTSize);
        }
        smoothFFT.reset(deviceFrameRate);
        smoothFFT.setAsym(FFT_ATK, FFT_RLS);

        return true;
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

    unsigned int getNumChannels() {
        return channels;
    }

    unsigned int getFirstAudibleIndex() {
        return firstAudibleIndex;
    }

    unsigned int getAudibleSize() {
        return audibleSize;
    }

    //if bypassing smoothedValue array, and want output buffer
    const float* getFFTPtr() {
        return fft->getOutputBuffer();
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
    //sets arbitrary size smoothAoS and finds midpoint for the below smoothing algo
    void setPixelBinIndices(size_t size) {
        pixelBinIndices.resize(size);
        bool firstHighPixelFound = false;

        const float scale = (float)fftSize / (float)sampleRate;

        for (int i = 0; i < size; ++i) {
            float normalized = (float)i / (float)(size - 1);
            float freq; //need convertFrom0to1 equivalent
            if (!firstHighPixelFound && freq > MID_FREQ) {
                firstHighPixel = i;
                firstHighPixelFound = true;
            }
            float binIndexFloat = freq * scale;

            pixelBinIndices[i] = std::min(std::max(binIndexFloat, 0.0f), (float)totalBinAmt);
        }
    }

    //smoothing based on arbitrary size for the purpose of per pixel smoothing
    //choose a freq as midpoint in const expr above, then cubic interp up to that point
    //will get non overlapping rms of each bucket after midpoint, places in smooth as dB
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

    //get low end interp using surrounding bins, since they are sparse here
    float getLowFreqSmoothedValue(int i, float* fftOut) {
        float centerBinFloat = pixelBinIndices[i];
        int bin1 = (int)centerBinFloat;
        float fraction = centerBinFloat - bin1;

        int bin0 = std::max(0, bin1 - 1);
        int bin2 = std::min(totalBinAmt - 1, bin1 + 1);
        int bin3 = std::min(totalBinAmt - 1, bin1 + 2);

        float y0 = fftOut[bin0];
        float y1 = fftOut[bin1];
        float y2 = fftOut[bin2];
        float y3 = fftOut[bin3];

        return cubicInterp(y0, y1, y2, y3, fraction);
    }

    //basically, gets rms of bounds from last bucket + 1 to current
    float getHighFreqSmoothedValues(int i, float* fftOut) {
        int lowB = (int)pixelBinIndices[i - 1] + 1;
        int highB = (int)pixelBinIndices[i];

        float sumSq = 0.0f;
        for (int i = lowB; i <= highB && i < fftSize; ++i) {
            float mag = dBToGain(fftOut[i], MIN_DB);
            sumSq += mag * mag;
        }
        float rms = std::sqrt(sumSq / (highB - lowB + 1));
        return gainToDB(rms, MIN_DB);
    }

    //pixel smoothing low end helper to cubic interpolate
    float cubicInterp(float y0, float y1, float y2, float y3, float mu) {
        //Catmull-Rom spline interpolation
        float mu2 = mu * mu;
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 = y1;

        return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
    }

    //if not using pixel smoothing array, this is the bin only alternative
    void binToSmooth() {
        float* fftPtr = fft->getOutputBuffer();
        float* smooth = smoothFFT.getTargets();
        for (int i = 0; i < audibleSize; ++i) {
            smooth[i] = fftPtr[i + firstAudibleIndex];
        }
    }

    //float to float gain/mag to dB helper
    float gainToDB(float mag, float minDB) {
        return std::max(minDB, 20.0f * std::log10(mag));
    }

    //float to float db to gain/mag helper
    float dBToGain(float dB, float minDB) {
        return std::max(minDB, std::pow(10.0f, dB * 0.05f));
    }

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
    int totalBinAmt = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;

    uint32_t firstAudibleIndex = 0;
    uint32_t audibleSize = 0;
    size_t smoothFFTSize;
    uint32_t firstHighPixel = 0;
};
