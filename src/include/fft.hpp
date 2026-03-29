#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <fftw3.h>

static constexpr float PI = 3.14159265358979323846f;

struct FFT{
    FFT(int N, bool per = true, bool win = true, int powerMagOrDB = 2,
        bool ss = true, float slope = 0.0f) : 
        n(N), isPerceptual(per), isWindowed(win), 
        outputMeasurement(powerMagOrDB), isSingleSided(ss), perceptualSlope(slope) {
        binAmt = n / 2 + 1;
        //initialize fft and precompute table
        in = (float *)fftwf_malloc(sizeof(float) * n);
        out = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * binAmt);
        p = fftwf_plan_dft_r2c_1d(n, in, out, FFTW_MEASURE);
        placement = (float *)out;
        if (isWindowed) {
            windowingTable.resize(n);
        }
        scalarTable.resize(binAmt);
    }

    ~FFT() {
        if (p)      fftwf_destroy_plan(p);
        if (in)     fftwf_free(in);
        if (out)    fftwf_free(out);
    }

    //no copies allowed
    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;

    //move is ok for the potential of placing these in vectors
    FFT(FFT&& other) noexcept : in(other.in), out(other.out), p(other.p),
                                placement(other.placement), n(other.n),
                                binAmt(other.binAmt), isPerceptual(other.isPerceptual),
                                isWindowed(other.isWindowed),
                                outputMeasurement(other.outputMeasurement),
                                isSingleSided(other.isSingleSided),
                                perceptualSlope(other.perceptualSlope),
                                windowingTable(std::move(other.windowingTable)),
                                scalarTable(std::move(other.scalarTable)) {
        other.in = nullptr;
        other.out = nullptr;
        other.p = nullptr;
        other.placement = nullptr;
    }
    FFT& operator=(FFT&& other) noexcept {
        if (this != &other) {
            fftwf_destroy_plan(p);
            fftwf_free(in);
            fftwf_free(out);

            in = other.in;
            out = other.out;
            p = other.p;
            placement = other.placement;
            n = other.n;
            binAmt = other.binAmt;
            isPerceptual = other.isPerceptual;
            isWindowed = other.isWindowed;
            outputMeasurement = other.outputMeasurement;
            isSingleSided = other.isSingleSided;
            perceptualSlope = other.perceptualSlope;
            windowingTable = std::move(other.windowingTable);
            scalarTable = std::move(other.scalarTable);
            other.in = nullptr;
            other.out = nullptr;
            other.p = nullptr;
            other.placement = nullptr;
        }
        return *this;
    }

    void initFFT(uint32_t sr) {
        fillScalarTable(sr);
        if (isWindowed) {
            fillWindowingTable();
        }
    }

    float* getInputBuffer() {
        return in;
    }

    const float* getOutputBuffer() {
        return placement;
    }

    uint32_t getTotalBins() {
        return binAmt;
    }

    //return first index then amount of bins used in spectral analysis
    void getAudibleRange(uint32_t sr, uint32_t* start, uint32_t* size) {
        const float binMult = (float)sr / (float) n;
        bool firstAudibleSet = false;

        for (int i = 0; i < binAmt; ++i) {
            float binFreq = (float)i * binMult;
            if (!firstAudibleSet && binFreq >= 20.0f) {
                *start = i;
                firstAudibleSet = true;
            }
            if (binFreq > 20000.0f) {
                *size = i - *start;
                break;
            }
        }
    }

    void swapSpec(bool isPer, bool isWin, int outputMeas, float slope, uint32_t sr) {
        //any change other than isWin will cause 
        //a recompute of the scalar table,
        //isWin only changes future processing cost, 
        //unless windowing table hasn't been filled yet
        if (isPer != isPerceptual || slope != perceptualSlope
                                  || outputMeasurement != outputMeas) {
            isPerceptual = isPer;
            perceptualSlope = slope;
            outputMeasurement = outputMeas;
            fillScalarTable(sr);
        }
        if (isWin && !windowTableFilled) {
            fillWindowingTable();
        }
    }

    void runFFT() {
        if (isWindowed && windowTableFilled) {
            multiplyWithWindowingTable();
        }
        fftwf_execute(p);
        switch (outputMeasurement) {
            case 0:  convertToPower(); multiplyWithScalarTable(); break;
            case 1:  convertToMag();   multiplyWithScalarTable(); break;
            case 2:  convertToPower(); multiplyWithScalarTable(); convertToDB(); break;
            default: convertToPower(); multiplyWithScalarTable(); convertToDB(); break;
        }
    }

private:
    void fillWindowingTable() {
        //fill table with hann window scalars
        for (uint32_t i = 0; i < n; ++i) {
            auto cos2 = std::cos(2 * i * PI / (n - 1));
            windowingTable[i] = 0.5f - 0.5f * cos2;
        }

        //get sum for normalize
        float sum = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            sum += windowingTable[i];
        }

        //normalize
        auto factor = (float)n / sum;
        for (uint32_t i = 0; i < n; ++i) {
            windowingTable[i] *= factor;
        }
        windowTableFilled = true;
    }

    void fillScalarTable(uint32_t sr) {
        //NOTE: accounts for single sided and loss from FFT ops at 2.0f each. 
        //Scale has been tested, it is consistently in line with peak and RMS
        const bool isMagnitude = (outputMeasurement == 1);
        float scaleNumerator = (isSingleSided) ? 4.0f : 2.0f;
        float scale = scaleNumerator / (float)n;
        scale = (isMagnitude) ? scale : scale * scale;
        float tiltExponent = (isPerceptual) ? perceptualSlope / 6.0206f : 0.0f;
        tiltExponent = (isMagnitude) ? tiltExponent : tiltExponent * 2;
        const float binMult = (float)sr / (float)n;

        scalarTable[0] = scale / 2.0f;
        for (uint32_t i = 1; i < binAmt - 1; ++i) {
            float binFreq = (float)i * binMult;
            float tilt = std::pow(binFreq / 1000.0f, tiltExponent);
            scalarTable[i] = tilt * scale;
        }
        //annoying
        scalarTable[binAmt - 1] = std::pow(((float)(binAmt - 1) * binMult) / 1000.0f, 
                                             tiltExponent) * (scale / 2);
    }

    void multiplyWithWindowingTable() {
        for (uint32_t i = 0; i < n; ++i) {
            in[i] *= windowingTable[i];
        }
    }

    void convertToMag() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            float real = out[i][0];
            float imag = out[i][1];
            float mag = std::sqrt(real * real + imag * imag);
            placement[i] = mag;
        }
    }

    void convertToPower() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            float real = out[i][0];
            float imag = out[i][1];
            float pow = real * real + imag * imag;
            placement[i] = pow;
        }
    }

    void multiplyWithScalarTable() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            placement[i] *= scalarTable[i];
        }
    }

    void convertToDB() {
        for (uint32_t i = 0; i < binAmt; ++i) {
            float mag = 10.0f * std::log10(placement[i]);
            placement[i] = std::max(mag, -96.0f);
        }
    }

    //NOTE: 
    //total byte size: ((3n + 1) * (n / 2 + 1) * sizeof(float)) + 82 + sizeof(plan) 

    //size: n * sizeof(float)
    float *in;
    //size: (n / 2 + 1) * sizeof(complex(8 bytes))
    fftwf_complex *out;
    //pointer to overwrite complexes after conversions
    float *placement;
    fftwf_plan p;
    bool windowTableFilled = false;
    bool isPerceptual;
    bool isWindowed;
    int outputMeasurement;
    bool isSingleSided;
    float perceptualSlope;
    uint32_t n;
    uint32_t binAmt;
    //size: n * sizeof(float)
    std::vector<float> windowingTable;
    //size: (n / 2 + 1) * sizeof(float)
    std::vector<float> scalarTable;
};

