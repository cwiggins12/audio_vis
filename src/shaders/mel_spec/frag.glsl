// ============================================================
// Mel Spectrogram Shader
//
// Spec config required:
//   fftOutputMode             = FULL_BIN
//   fftOutputMeasurement      = DECIBELS
//   perceptualSlopeDegrees    = 0.0f
//   useFFTSmoothing           = false
//   getsFFTHolds              = false
//   feedbackBufferSizeExpr    = "W * 4097 + 1"
//   feedbackBufferInitValue   = -90.0f
//
// feedbackBuffer layout:
//   [col * numBins + bin]  for col in [0, W), bin in [0, numBins)
//
// Frequency axis: mel-warped, 20hz at bottom to min(20khz, nyquist) at top
// Time axis:      newest data on the right, scrolls left each frame
// Color:          inferno (black -> purple -> orange -> yellow)
// ============================================================

const float DB_FLOOR = -90.0;
const float DB_CEIL  =   0.0;

// --- Mel helpers ---

float hzToMel(float hz) {
    return 2595.0 * log(1.0 + hz / 700.0) / log(10.0);
}

float melToHz(float mel) {
    return 700.0 * (pow(10.0, mel / 2595.0) - 1.0);
}

// Map uv.y [0,1] to a linear bin index via mel scale.
// Uses true bin hz via binWidth — no assumptions about edge frequencies.
// Bottom of screen = low freq, top = high freq.
int melBin(float uvY) {
    float binWidth = float(sampleRate) / 8192.0;
    float melMin   = hzToMel(20.0);
    float melMax   = hzToMel(min(20000.0, float(sampleRate) * 0.5));
    float hz       = melToHz(mix(melMin, melMax, uvY));
    return clamp(int(hz / binWidth), 0, numBins - 1);
}

// --- Colormap: inferno ---
// black -> deep purple -> crimson -> orange -> bright yellow

vec3 inferno(float t) {
    t = clamp(t, 0.0, 1.0);

    vec3 c0 = vec3(0.000, 0.000, 0.000); // black
    vec3 c1 = vec3(0.220, 0.020, 0.420); // deep purple
    vec3 c2 = vec3(0.660, 0.140, 0.240); // crimson
    vec3 c3 = vec3(0.960, 0.460, 0.060); // orange
    vec3 c4 = vec3(0.988, 0.998, 0.644); // bright yellow

    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    return             mix(c3, c4, (t - 0.75) / 0.25);
}

float normalizeDB(float db) {
    return clamp((db - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.0, 1.0);
}

// --- Main ---

void main() {
    int fragCol = clamp(int(uv.x * W), 0, int(W) - 1);
    int bin     = melBin(uv.y);
    int W_int   = int(W);

    // Scroll: shift history left, write current FFT into rightmost column
    if (fragCol < W_int - 1) {
        feedbackOut[fragCol * numBins + bin] = feedbackIn[(fragCol + 1) * numBins + bin];
    } else {
        feedbackOut[fragCol * numBins + bin] = fftData[bin];
    }

    // Render from feedbackIn (last committed frame)
    float db  = feedbackIn[fragCol * numBins + bin];
    FragColor = vec4(inferno(normalizeDB(db)), 1.0);
}
