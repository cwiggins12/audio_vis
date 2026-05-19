// ============================================================
// feedback fractal - frag.glsl
// ============================================================
//
// Fractal-like structures emerge from the feedback loop itself.
// Small bright dots drawn near center get duplicated at shrinking
// scales by the zoom, twisted into spirals by rotation, and
// persist through high decay. No fractal math — pure emergence.
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


// ======================== CONSTANTS ========================

const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- Warp ---
const float BASE_ZOOM     = 1.06;
const float BASS_ZOOM     = 0.05;
const float BASE_ROT      = 0.015;
const float TREB_ROT      = 0.04;
const float WARP_AMOUNT   = 0.01;
const float WARP_SPEED    = 0.5;
const float WARP_SCALE    = 2.0;

// --- Decay ---
const float DECAY         = 0.985;

// --- Seeds ---
const int   NUM_SEEDS     = 12;
const float SEED_ORBIT    = 0.08;
const float SEED_SIZE     = 0.01;
const float SEED_SOFT     = 0.02;
const float SEED_BRIGHT   = 1.0;

// --- Waveform ---
const float WAVE_THICK    = 0.04;    // waveform line thickness (in NDC)

// --- Color ---
const float COLOR_CYCLE   = 0.04;
const vec3  COLOR_TINT    = vec3(0.97, 0.94, 1.0);
const float VIGNETTE_AMT  = 0.5;

// --- Band Tracking ---
const float ATT_RATE      = 0.92;
const float AVG_RATE      = 0.995;


// ===================== BAND TRACKING =======================

const int FB_BASS_ATT = 0;
const int FB_MID_ATT  = 1;
const int FB_TREB_ATT = 2;
const int FB_BASS_AVG = 3;
const int FB_MID_AVG  = 4;
const int FB_TREB_AVG = 5;
const int FB_PIX_OFFSET = 6;

float bass, mid, treb;
float bass_att, mid_att, treb_att;
float bass_avg, mid_avg, treb_avg;
float bass_rel, mid_rel, treb_rel;

void readBands() {
    bass = fftData[0];
    mid  = fftData[1];
    treb = fftData[2];
    bass_att = feedbackIn[FB_BASS_ATT];
    mid_att  = feedbackIn[FB_MID_ATT];
    treb_att = feedbackIn[FB_TREB_ATT];
    bass_avg = feedbackIn[FB_BASS_AVG];
    mid_avg  = feedbackIn[FB_MID_AVG];
    treb_avg = feedbackIn[FB_TREB_AVG];
    bass_att = mix(bass, bass_att, ATT_RATE);
    mid_att  = mix(mid,  mid_att,  ATT_RATE);
    treb_att = mix(treb, treb_att, ATT_RATE);
    bass_avg = mix(bass, bass_avg, AVG_RATE);
    mid_avg  = mix(mid,  mid_avg,  AVG_RATE);
    treb_avg = mix(treb, treb_avg, AVG_RATE);
    bass_rel = bass / max(bass_avg, 0.001);
    mid_rel  = mid  / max(mid_avg,  0.001);
    treb_rel = treb / max(treb_avg, 0.001);
}

void writeBands() {
    feedbackOut[FB_BASS_ATT] = bass_att;
    feedbackOut[FB_MID_ATT]  = mid_att;
    feedbackOut[FB_TREB_ATT] = treb_att;
    feedbackOut[FB_BASS_AVG] = bass_avg;
    feedbackOut[FB_MID_AVG]  = mid_avg;
    feedbackOut[FB_TREB_AVG] = treb_avg;
}


// ================== FEEDBACK FRAMEBUFFER ===================

vec4 readPixel(vec2 uv) {
    uv = clamp(uv, 0.0, 1.0);
    int ix = int(uv.x * W);
    int iy = int(uv.y * H);
    ix = clamp(ix, 0, int(W) - 1);
    iy = clamp(iy, 0, int(H) - 1);
    int idx = FB_PIX_OFFSET + (iy * int(W) + ix) * 4;
    return vec4(
        feedbackIn[idx + 0],
        feedbackIn[idx + 1],
        feedbackIn[idx + 2],
        feedbackIn[idx + 3]
    );
}

void writePixel(vec4 color, vec2 px) {
    int ix = clamp(int(px.x), 0, int(W) - 1);
    int iy = clamp(int(px.y), 0, int(H) - 1);
    int idx = FB_PIX_OFFSET + (iy * int(W) + ix) * 4;
    feedbackOut[idx + 0] = color.r;
    feedbackOut[idx + 1] = color.g;
    feedbackOut[idx + 2] = color.b;
    feedbackOut[idx + 3] = color.a;
}


// ========================= WARP ============================

vec2 warpUV(vec2 uv) {
    vec2 c = uv - 0.5;

    float zoom = BASE_ZOOM + (bass - 1.0) * BASS_ZOOM;
    c /= zoom;

    float rot = BASE_ROT + (treb - 1.0) * TREB_ROT;
    float cs = cos(rot);
    float sn = sin(rot);
    c = vec2(c.x * cs - c.y * sn, c.x * sn + c.y * cs);

    vec2 warp = vec2(
        sin(c.y * WARP_SCALE * TWO_PI + time * WARP_SPEED),
        cos(c.x * WARP_SCALE * TWO_PI + time * WARP_SPEED * 0.7)
    );
    c += warp * WARP_AMOUNT * (1.0 + (mid - 1.0) * 0.5);

    return c + 0.5;
}


// ======================== HELPERS ==========================

vec3 hsv2rgb(float h, float s, float v) {
    h = fract(h);
    float c = v * s;
    float x = c * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
    float m = v - c;
    vec3 rgb;
    if      (h < 1.0/6.0) rgb = vec3(c, x, 0.0);
    else if (h < 2.0/6.0) rgb = vec3(x, c, 0.0);
    else if (h < 3.0/6.0) rgb = vec3(0.0, c, x);
    else if (h < 4.0/6.0) rgb = vec3(0.0, x, c);
    else if (h < 5.0/6.0) rgb = vec3(x, 0.0, c);
    else                   rgb = vec3(c, 0.0, x);
    return rgb + m;
}


// ===================== DRAW CONTENT ========================

vec3 drawSeeds(vec3 existing, vec2 ndc) {
    vec3 col = existing;

    float orbit = SEED_ORBIT * (1.0 + (bass_att - 1.0) * 0.4);

    for (int i = 0; i < NUM_SEEDS; i++) {
        float angle = float(i) / float(NUM_SEEDS) * TWO_PI;
        angle += time * 0.8 + mid_att * 0.5;

        vec2 seedPos = vec2(cos(angle), sin(angle)) * orbit;
        float dist = length(ndc - seedPos);

        float mask = smoothstep(SEED_SIZE + SEED_SOFT, SEED_SIZE - SEED_SOFT, dist);

        float hue = fract(float(i) / float(NUM_SEEDS) + time * COLOR_CYCLE);
        float sat = 0.7 + (treb - 1.0) * 0.15;
        float val = SEED_BRIGHT * (0.8 + bass * 0.2);
        vec3 seedColor = hsv2rgb(hue, sat, val);

        col = mix(col, seedColor, mask);
    }

    return col;
}

vec3 drawWaveRing(vec3 existing, vec2 ndc) {
    float dist = length(ndc);
    float angle = atan(ndc.y, ndc.x);
    float t = (angle + PI) / TWO_PI;

    int sampleIdx = clamp(int(t * float(hopSize)), 0, hopSize - 1);
    float samp = rawSamples[sampleIdx];

    float ringR = 0.2 + (bass_att - 1.0) * 0.05;
    float wavePos = ringR + samp * 0.08;
    float waveDist = abs(dist - wavePos);
    float waveMask = smoothstep(WAVE_THICK, 0.0, waveDist);

    float hue = fract(time * COLOR_CYCLE * 0.5 + 0.5);
    vec3 wc = hsv2rgb(hue, 0.5, 0.5);

    return mix(existing, wc, waveMask * 0.5);
}

vec3 drawContent(vec3 existing, vec2 uv, vec2 ndc) {
    vec3 col = existing;
    col = drawSeeds(col, ndc);
    col = drawWaveRing(col, ndc);
    return col;
}


// ==================== POST-PROCESS =========================

vec3 postProcess(vec3 col, vec2 uv) {
    col *= COLOR_TINT;

    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, 1.2);

    float transient = clamp(bass - 1.3, 0.0, 0.4);
    col += col * transient;

    return clamp(col, 0.0, 1.0);
}


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 px  = toPx();
    vec2 ndc = ndcBottomLeftAR();

    readBands();

    vec2 warpedUV = warpUV(uv);
    vec4 prev = readPixel(warpedUV);
    vec3 col = prev.rgb;

    col *= DECAY;

    col = drawContent(col, uv, ndc);

    col = postProcess(col, uv);

    writeBands();
    writePixel(vec4(col, 1.0), px);

    FragColor = vec4(col, 1.0);
}
