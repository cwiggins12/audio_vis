const float DB_FLOOR        = -90.0;
const float DB_CEIL         = 0.0;
const int   TOTAL_MEL_BINS  = 256;
const int   SECONDS_SHOWN   = 5;

float hzToMel(float hz) {
    return 2595.0 * log(1.0 + hz / 700.0) / log(10.0);
}

float melToHz(float mel) {
    return 700.0 * (pow(10.0, mel / 2595.0) - 1.0);
}

float melBinEnergy(int m) {
    float melMin = hzToMel(20.0);
    float melMax = hzToMel(min(20000.0, float(sampleRate) * 0.5));

    float melLeft   = mix(melMin, melMax, float(m)     / float(TOTAL_MEL_BINS + 1));
    float melCenter = mix(melMin, melMax, float(m + 1) / float(TOTAL_MEL_BINS + 1));
    float melRight  = mix(melMin, melMax, float(m + 2) / float(TOTAL_MEL_BINS + 1));

    float binWidth = float(sampleRate) / float(fftSize);
    int kLeft   = clamp(int(melToHz(melLeft)   / binWidth), 0, fftArrSize - 1);
    int kCenter = clamp(int(melToHz(melCenter) / binWidth), 0, fftArrSize - 1);
    int kRight  = clamp(int(melToHz(melRight)  / binWidth), 0, fftArrSize - 1);

    float energy = 0.0;
    float weightSum = 0.0;

    for (int k = kLeft; k <= kCenter; k++) {
        float w = float(k - kLeft) / float(max(kCenter - kLeft, 1));
        energy     += w * fftData[k];
        weightSum  += w;
    }

    for (int k = kCenter + 1; k <= kRight; k++) {
        float w = float(kRight - k) / float(max(kRight - kCenter, 1));
        energy     += w * fftData[k];
        weightSum  += w;
    }

    return (weightSum > 0.0) ? energy / weightSum : 0.0;
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
    return               mix(c3, c4, (t - 0.75) / 0.25);
}

float normalizeDB(float db) {
    return clamp((db - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.0, 1.0);
}

float powToDB(float pow) {
    return 10.0 * log(pow + 1e-9) / log(10.0);
}

void main() {
    vec2 uv = uvBottomLeft();
    int columnAmt = int(48000.0 * 4.0 / 8192.0 * float(SECONDS_SHOWN));
    float columnWidth = 1.0 / float(columnAmt);
    int m = int(uv.y * float(TOTAL_MEL_BINS));
    m = clamp(m, 0, TOTAL_MEL_BINS - 1);
    int column = int(uv.x * float(columnAmt));
    int index = column * TOTAL_MEL_BINS + m;
    if (newAudioWindow != 0) {
        if (column == columnAmt - 1) {
            //get mel norm, write, get color and print
            float energy = melBinEnergy(m);
            float db = powToDB(energy);
            float norm = normalizeDB(db);
            feedbackOut[index] = norm;
            FragColor = vec4(inferno(norm), 1.0);
        }
        else if (column == 0) {
            //read, get color, print, no write
            FragColor = vec4(inferno(feedbackIn[index]), 1.0);
        }
        else {
            //read, write to index - TOTAL_MEL_BINS, print color
            float val = feedbackIn[index + TOTAL_MEL_BINS];
            feedbackOut[index] = val;
            FragColor = vec4(inferno(val), 1.0);
        }
    }
    else {
        //read from proper index, then write to same index here
        float val = feedbackIn[index];
        feedbackOut[index] = val;
        FragColor = vec4(inferno(val), 1.0);
    }
}

