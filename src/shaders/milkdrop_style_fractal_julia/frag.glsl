// ============================================================
// julia morph - frag.glsl
// ===========================================================
//
// Full Julia set visible at all times, continuously morphing.
// Uses curated c-values that are known to produce visually rich
// Julia sets, with smooth interpolation between them. View is
// offset toward the fractal boundary so detail fills the screen.
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


// ======================== CONSTANTS ========================

const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- C-Parameter ---
const float C_DRIFT_SPEED   = 0.003;    // base speed through c-value path
const float C_BASS_ACCEL    = 0.003;    // bass accelerates the drift
const float C_MID_PERTURB  = 0.0025;   // treble perturbs c off the path

// --- View ---
const float VIEW_SCALE      = 1.1;     // how much fractal space is visible
const float VIEW_BASS_SCALE  = -0.07;    // bass breathes the scale
// view center offset factor: shifts toward boundary
const float VIEW_CENTER_AMT = 0.2;

// --- Rotation ---
const float BASE_ROT_RATE   = 0.002;
const float TREB_ROT_EXTRA  = 0.005;

// --- Iteration ---
const int   MAX_ITER        = 100;

// --- Color ---
const float COLOR_DENSITY   = 4.0;
const float COLOR_CYCLE     = 0.03;
const float COLOR_SAT       = 0.8;
const float INTERIOR_BRIGHT = 0.55;
const float EXTERIOR_BRIGHT = 0.65;
const vec3  COLOR_TINT      = vec3(1.0, 0.97, 0.95);
const float VIGNETTE_AMT    = 0.4;

// --- Band Tracking ---
const float ATT_RATE        = 0.95;
const float AVG_RATE        = 0.995;


// ================== FEEDBACK BUFFER LAYOUT =================
const int FB_BASS_ATT   = 0;
const int FB_MID_ATT    = 1;
const int FB_TREB_ATT   = 2;
const int FB_BASS_AVG   = 3;
const int FB_MID_AVG    = 4;
const int FB_TREB_AVG   = 5;
const int FB_C_ANGLE    = 6;
const int FB_ROTATION   = 7;


// ===================== BAND TRACKING =======================

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

// Curated c-values that always produce visually rich Julia sets.
// Smooth hermite interpolation between them guarantees we're
// always near an interesting region of c-space.
vec2 getInterestingC(float t) {
    vec2 c0 = vec2(-0.70,  0.27);   // spiral
    vec2 c1 = vec2(-0.54,  0.54);   // branching filaments
    vec2 c2 = vec2( 0.355, 0.355);  // dendrite
    vec2 c3 = vec2(-0.4,   0.6);    // rabbit
    vec2 c4 = vec2(-0.75,  0.11);   // Douady rabbit
    vec2 c5 = vec2( 0.28,  0.008);  // near main antenna
    vec2 c6 = vec2(-0.12,  0.74);   // Siegel disk boundary
    vec2 c7 = vec2(-1.25,  0.0);    // basilica

    float segment = fract(t) * 8.0;
    int idx = int(floor(segment));
    float frac = fract(segment);
    frac = frac * frac * (3.0 - 2.0 * frac);

    vec2 ca, cb;
    if      (idx == 0) { ca = c0; cb = c1; }
    else if (idx == 1) { ca = c1; cb = c2; }
    else if (idx == 2) { ca = c2; cb = c3; }
    else if (idx == 3) { ca = c3; cb = c4; }
    else if (idx == 4) { ca = c4; cb = c5; }
    else if (idx == 5) { ca = c5; cb = c6; }
    else if (idx == 6) { ca = c6; cb = c7; }
    else               { ca = c7; cb = c0; }

    return mix(ca, cb, frac);
}


// ===================== JULIA RENDER ========================

vec3 renderJulia(vec2 ndc, float cProgress, float viewAngle) {
    // rotate the view
    float cs = cos(viewAngle);
    float sn = sin(viewAngle);
    vec2 rotated = vec2(ndc.x * cs - ndc.y * sn,
                        ndc.x * sn + ndc.y * cs);

    // get c from the curated path
    vec2 c = getInterestingC(cProgress);
    c.x += (mid - 1.0) * C_MID_PERTURB;
    c.y += (treb - 1.0) * C_MID_PERTURB;
    float cx = c.x;
    float cy = c.y;

    // view scale
    float scale = VIEW_SCALE + (bass_att - 1.0) * VIEW_BASS_SCALE;

    // offset view toward the fractal boundary
    vec2 viewCenter = c * VIEW_CENTER_AMT;

    vec2 z = rotated * scale + viewCenter;

    // orbit traps
    float trapOrigin = 1e10;
    float trapCircle = 1e10;
    float trapLine   = 1e10;

    float smoothIter = 0.0;
    bool escaped = false;
    for (int i = 0; i < MAX_ITER; i++) {
        z = vec2(z.x * z.x - z.y * z.y + cx,
                 2.0 * z.x * z.y + cy);

        float zLen = length(z);
        trapOrigin = min(trapOrigin, zLen);
        trapCircle = min(trapCircle, abs(zLen - 1.0));
        trapLine   = min(trapLine, abs(z.x) + abs(z.y));

        if (dot(z, z) > 256.0) {
            smoothIter = float(i) - log2(log2(dot(z, z))) + 4.0;
            escaped = true;
            break;
        }
    }

    float t0 = clamp(trapOrigin * 0.4, 0.0, 1.0);
    float tc = clamp(1.0 - trapCircle * 2.5, 0.0, 1.0);
    float tl = clamp(1.0 - trapLine * 0.5, 0.0, 1.0);
    float trapMix = t0 * 0.3 + tc * 0.35 + tl * 0.35;

    float hue, sat, val;

    if (escaped) {
        float t = smoothIter / float(MAX_ITER);
        hue = fract(t * COLOR_DENSITY + cProgress * 0.2 + time * COLOR_CYCLE);
        sat = COLOR_SAT + t * 0.15;
        val = EXTERIOR_BRIGHT * (0.3 + t * 0.8);
    } else {
        hue = fract(trapMix * COLOR_DENSITY * 0.5 + cProgress * 0.2 + time * COLOR_CYCLE + 0.3);
        sat = COLOR_SAT * (0.5 + trapMix * 0.5);
        val = INTERIOR_BRIGHT * (0.2 + trapMix * 0.8);
    }

    val *= 0.85 + bass * 0.05;
    sat = clamp(sat + (mid - 1.0) * 0.04, 0.3, 0.9);

    return hsv2rgb(hue, sat, val);
}

vec3 waveform() {
    vec2 uv = uvBottomLeft();
    int ind = int(float(hopSize) * uv.x);
    float s = rawSamples[ind];
    float waveY = (s * 0.5 + 0.5) * H;
    vec2 px = toPx();
    float dist = abs(px.y - waveY);
    float line = smoothstep(8.0, 0.0, dist);

    vec3 col = vec3(1-sin(time), sin(time), bass);
    return col * line;

}

// ==================== POST-PROCESS =========================

vec3 postProcess(vec3 col, vec2 uv) {
    col *= COLOR_TINT;

    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    col = smoothstep(0.0, 1.0, col);

    //float transient = clamp(0.3, 0.0, 0.3);
    col += col * 0.9;

    return clamp(col, 0.0, 1.0);
}


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 ndc = ndcBottomLeftAR();

    readBands();

    float cAngle    = feedbackIn[FB_C_ANGLE];
    float viewAngle = feedbackIn[FB_ROTATION];

    // advance through curated c-value path
    float cSpeed = C_DRIFT_SPEED + max(bass_att - 1.0, 0.0) * C_BASS_ACCEL;
    cAngle += cSpeed;

    // advance rotation
    float rotSpeed = BASE_ROT_RATE + max(treb - 1.0, 0.0) * TREB_ROT_EXTRA;
    viewAngle += rotSpeed;
    viewAngle = mod(viewAngle, TWO_PI);

    // cProgress indexes into getInterestingC via fract()
    // * 0.1 means one full cycle through all 8 c-values
    // takes cAngle going from 0 to 10
    float cProgress = cAngle * 0.1;

    vec3 col = renderJulia(ndc, cProgress, viewAngle);
    col = postProcess(col, uv);
    float opacity = mid * 0.1;
    vec3 wave = waveform();
    col = mix(col, wave, opacity);

    writeBands();
    feedbackOut[FB_C_ANGLE]  = cAngle;
    feedbackOut[FB_ROTATION] = viewAngle;

    FragColor = vec4(col, 1.0);
}
