// ─── mel spectrogram ─────────────────────────────────────────────────────────
// spec config:
//   fftOutputMode              = AUDIBLE_BIN
//   fftOutputMeasurement       = POWER
//   isFFTHannWindowed          = true
//   useFFTSmoothing            = false
//   perceptualSlopeDegrees     = 0.0
//   feedbackBufferScalesWithWindow = WIDTH_SCALE
//   feedbackBufferSizeExpr     = "WINDOW_WIDTH * 256"
//   feedbackBufferInitValue    = 0.0

// ─── constants ───────────────────────────────────────────────────────────────

const int   MEL_BINS     = 256;
const int   FFT_SIZE     = 8192;
const float MEL_MIN_HZ   = 20.0;
const float MEL_MAX_HZ   = 20000.0;
const float DB_FLOOR     = -80.0;
const float DB_CEIL      =   0.0;

// log10 via change of base
float log10(float x) { return log2(x) / log2(10.0); }

// ─── mel / hz conversion ─────────────────────────────────────────────────────

float hzToMel(float hz) {
    return 2595.0 * log10(1.0 + hz / 700.0);
}

float melToHz(float mel) {
    return 700.0 * (pow(10.0, mel / 2595.0) - 1.0);
}

// ─── filterbank ──────────────────────────────────────────────────────────────
// computes one mel band's power by walking the triangular filter
// over the audible power bins.
// binCount  = numBins (audible bins passed in from cpu)
// sampleRateF = float(sampleRate)

float melBandPower(int band, int binCount, float sampleRateF) {
    float melMin  = hzToMel(MEL_MIN_HZ);
    float melMax  = hzToMel(MEL_MAX_HZ);
    float melStep = (melMax - melMin) / float(MEL_BINS + 1);

    // center and neighbours in mel space
    float melLo  = melMin + float(band)     * melStep;
    float melCtr = melMin + float(band + 1) * melStep;
    float melHi  = melMin + float(band + 2) * melStep;

    // convert to hz then to bin indices (fractional)
    float hzLo   = melToHz(melLo);
    float hzCtr  = melToHz(melCtr);
    float hzHi   = melToHz(melHi);

    // bin index = freq * fftSize / sampleRate
    // audible bins start at bin index for MEL_MIN_HZ, but fftData[0]
    // corresponds to the first audible bin, so we offset accordingly
    float audibleOffset = MEL_MIN_HZ * float(FFT_SIZE) / sampleRateF;
    float binLo   = hzLo  * float(FFT_SIZE) / sampleRateF - audibleOffset;
    float binCtr  = hzCtr * float(FFT_SIZE) / sampleRateF - audibleOffset;
    float binHi   = hzHi  * float(FFT_SIZE) / sampleRateF - audibleOffset;

    int iLo  = int(floor(binLo));
    int iHi  = int(ceil(binHi));
    iLo = max(iLo, 0);
    iHi = min(iHi, binCount - 1);

    float power = 0.0;
    float weightSum = 0.0;

    for (int i = iLo; i <= iHi; i++) {
        float fi = float(i);
        float w  = 0.0;

        if (fi >= binLo && fi <= binCtr) {
            w = (fi - binLo) / (binCtr - binLo);
        } else if (fi > binCtr && fi <= binHi) {
            w = (binHi - fi) / (binHi - binCtr);
        }

        power     += w * fftData[i];
        weightSum += w;
    }

    // normalise by weight sum to keep bands comparable regardless of width
    return (weightSum > 0.0) ? power / weightSum : 0.0;
}

// ─── power to dB ─────────────────────────────────────────────────────────────

float toDb(float power) {
    float db = 10.0 * log10(max(power, 1e-10));
    return clamp((db - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.0, 1.0);
}

// ─── colormap (inferno) ───────────────────────────────────────────────────────
// piecewise linear approximation of matplotlib inferno

vec3 inferno(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    if (t < 0.25) {
        float s = t / 0.25;
        c = mix(vec3(0.0,   0.0,   0.0  ),
                vec3(0.18,  0.05,  0.42 ), s);
    } else if (t < 0.5) {
        float s = (t - 0.25) / 0.25;
        c = mix(vec3(0.18,  0.05,  0.42 ),
                vec3(0.62,  0.07,  0.42 ), s);
    } else if (t < 0.75) {
        float s = (t - 0.5) / 0.25;
        c = mix(vec3(0.62,  0.07,  0.42 ),
                vec3(0.96,  0.53,  0.14 ), s);
    } else {
        float s = (t - 0.75) / 0.25;
        c = mix(vec3(0.96,  0.53,  0.14 ),
                vec3(1.0,   1.0,   1.0  ), s);
    }
    return c;
}

// ─── main ─────────────────────────────────────────────────────────────────────

void main() {
    vec2  px         = toPx();
    float sampleRateF = float(sampleRate);

    // total columns stored in the feedback buffer
    int totalCols = int(W);

    // which historical column does this pixel's x correspond to?
    // column 0 (left) = oldest, column totalCols-1 (right) = newest
    int pixelCol  = int(px.x);
    int age       = totalCols - 1 - pixelCol;   // 0 = newest
    int readCol   = (frameCount - age + totalCols * 1024) % totalCols;

    // which mel bin does this pixel's y correspond to?
    // y=0 (bottom) = low freq, y=H (top) = high freq
    int melBin = int((px.y / H) * float(MEL_BINS));
    melBin = clamp(melBin, 0, MEL_BINS - 1);

    // ── write current frame into feedback ────────────────────────────────────
    // only the thread whose pixelCol == newest column does the write.
    // each thread writes its mel bin row for that column.
    if (pixelCol == totalCols - 1) {
        float power = melBandPower(melBin, numBins, sampleRateF);
        feedbackOut[frameCount * MEL_BINS + melBin] = power;
    }

    // ── read history and display ──────────────────────────────────────────────
    float power  = feedbackIn[readCol * MEL_BINS + melBin];
    float dbNorm = toDb(power);

    FragColor = vec4(inferno(dbNorm), 1.0);
}
