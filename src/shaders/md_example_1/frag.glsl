// ============================================================
// milkdrop style template - frag.glsl
// ============================================================
//
// Architecture:
//   1. Read 3-band audio (bass/mid/treb) from fftData[0..2]
//   2. Maintain _att and _avg variants via feedback buffer
//   3. Read previous frame from feedback, warp it (zoom/rot/distort)
//   4. Apply decay so old content fades
//   5. Draw new content on top (waveform, shapes, etc.)
//   6. Post-process (color grade, vignette, etc.)
//   7. Write composited frame back to feedback buffer
//
// The feedback buffer layout:
//   [0..5]  : bass_att, mid_att, treb_att, bass_avg, mid_avg, treb_avg
//   [6...]  : RGBA pixel data, row-major from bottom-left
//
// To make your own preset: tweak the constants below and modify
// drawContent() and postProcess(). The warp section has several
// preset suggestions in comments.
//
// ============================================================


// ======================== CONSTANTS ========================

const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- Warp Parameters ---
// These control how the previous frame is distorted each cycle.
// zoom > 1.0 = zooming in (content shrinks toward center)
// zoom < 1.0 = zooming out (content expands from center)
const float BASE_ZOOM     = 1.05;     // base zoom per frame
const float BASS_ZOOM     = 0.007;     // additional zoom driven by bass
const float BASE_ROT      = 0.005;    // base rotation per frame (radians)
const float TREB_ROT      = 0.06;     // additional rotation driven by treble
const float WARP_AMOUNT   = 0.009;     // sinusoidal UV warp intensity
const float WARP_SPEED    = 1.0;      // speed of the warp animation
const float WARP_SCALE    = 4.0;      // spatial frequency of the warp pattern

// --- Decay ---
// How quickly the previous frame fades. 1.0 = no fade, 0.0 = instant clear
const float DECAY         = 0.98;

// --- Waveform ---
const float WAVE_THICK    = 0.02;    // waveform line thickness (in NDC)
const float WAVE_BRIGHT   = 1.0;      // waveform brightness
const vec3  WAVE_COLOR    = vec3(1.0, 1.0, 1.0);

// --- Center Shape ---
const float SHAPE_RADIUS  = 0.1;     // base radius of center shape
const float SHAPE_PULSE   = 0.1;     // how much bass expands the shape
const float SHAPE_EDGE    = 0.1;    // softness of the shape edge

// --- Color ---
const vec3  COLOR_TINT    = vec3(0.825, 0.825, 0.8);  // overall color tint
const float VIGNETTE_AMT  = 0.4;      // vignette darkness (0 = none)

// --- Band Tracking ---
// att = attenuated, responds to sustained energy over ~0.3-0.5 seconds
// avg = long-range average over several seconds
// These rates are per-frame multipliers (higher = slower response)
const float ATT_RATE      = 0.92;
const float AVG_RATE      = 0.995;

// Preset suggestions (uncomment one block to try):
//
// --- Tunnel Zoom ---
// const float BASE_ZOOM = 1.03; const float BASS_ZOOM = 0.04;
// const float BASE_ROT = 0.0;   const float TREB_ROT = 0.0;
// const float DECAY = 0.97;     const float WARP_AMOUNT = 0.0;
//
// --- Kaleidoscope Spin ---
// const float BASE_ZOOM = 1.005; const float BASS_ZOOM = 0.01;
// const float BASE_ROT = 0.03;   const float TREB_ROT = 0.05;
// const float DECAY = 0.94;      const float WARP_AMOUNT = 0.02;
//
// --- Breathing Pulse ---
// const float BASE_ZOOM = 0.998; const float BASS_ZOOM = 0.03;
// const float BASE_ROT = 0.001;  const float TREB_ROT = 0.005;
// const float DECAY = 0.92;      const float WARP_AMOUNT = 0.005;
//
// --- Melting Warp ---
// const float BASE_ZOOM = 1.002; const float BASS_ZOOM = 0.01;
// const float BASE_ROT = 0.0;    const float TREB_ROT = 0.01;
// const float DECAY = 0.97;      const float WARP_AMOUNT = 0.04;
// const float WARP_SCALE = 2.0;


// ===================== BAND TRACKING =======================
// Indices into feedback buffer for the 6 tracked band values

const int FB_BASS_ATT = 0;
const int FB_MID_ATT  = 1;
const int FB_TREB_ATT = 2;
const int FB_BASS_AVG = 3;
const int FB_MID_AVG  = 4;
const int FB_TREB_AVG = 5;
const int FB_PIX_OFFSET = 6;  // pixel data starts here

// Band values available after readBands():
float bass, mid, treb;
float bass_att, mid_att, treb_att;
float bass_avg, mid_avg, treb_avg;
// Relative values (current / average). > 1.0 means louder than usual
float bass_rel, mid_rel, treb_rel;

void readBands() {
    // current values from engine FFT (magnitude, 0.0 to ~1.0)
    bass = fftData[0];
    mid  = fftData[1];
    treb = fftData[2];

    // read previous attenuated and average values from feedback
    bass_att = feedbackIn[FB_BASS_ATT];
    mid_att  = feedbackIn[FB_MID_ATT];
    treb_att = feedbackIn[FB_TREB_ATT];
    bass_avg = feedbackIn[FB_BASS_AVG];
    mid_avg  = feedbackIn[FB_MID_AVG];
    treb_avg = feedbackIn[FB_TREB_AVG];

    // update attenuated values (envelope follower, medium speed)
    bass_att = mix(bass, bass_att, ATT_RATE);
    mid_att  = mix(mid,  mid_att,  ATT_RATE);
    treb_att = mix(treb, treb_att, ATT_RATE);

    // update long-range averages (very slow)
    bass_avg = mix(bass, bass_avg, AVG_RATE);
    mid_avg  = mix(mid,  mid_avg,  AVG_RATE);
    treb_avg = mix(treb, treb_avg, AVG_RATE);

    // relative intensity: how loud is this moment compared to the average?
    // clamp to avoid division by near-zero
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
    float fx = uv.x * W - 0.5;
    float fy = uv.y * H - 0.5;
    int x0 = clamp(int(floor(fx)), 0, int(W) - 1);
    int y0 = clamp(int(floor(fy)), 0, int(H) - 1);
    int x1 = clamp(x0 + 1, 0, int(W) - 1);
    int y1 = clamp(y0 + 1, 0, int(H) - 1);
    float sx = fx - floor(fx);
    float sy = fy - floor(fy);

    int i00 = FB_PIX_OFFSET + (y0 * int(W) + x0) * 4;
    int i10 = FB_PIX_OFFSET + (y0 * int(W) + x1) * 4;
    int i01 = FB_PIX_OFFSET + (y1 * int(W) + x0) * 4;
    int i11 = FB_PIX_OFFSET + (y1 * int(W) + x1) * 4;

    vec4 c00 = vec4(feedbackIn[i00], feedbackIn[i00+1], feedbackIn[i00+2], feedbackIn[i00+3]);
    vec4 c10 = vec4(feedbackIn[i10], feedbackIn[i10+1], feedbackIn[i10+2], feedbackIn[i10+3]);
    vec4 c01 = vec4(feedbackIn[i01], feedbackIn[i01+1], feedbackIn[i01+2], feedbackIn[i01+3]);
    vec4 c11 = vec4(feedbackIn[i11], feedbackIn[i11+1], feedbackIn[i11+2], feedbackIn[i11+3]);

    vec4 top = mix(c01, c11, sx);
    vec4 bot = mix(c00, c10, sx);
    return mix(bot, top, sy);
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
// Distort UVs to sample the previous frame with zoom, rotation,
// and sinusoidal displacement. This is where the Milkdrop "motion"
// comes from. Modify this to create different motion styles.

vec2 warpUV(vec2 uv) {
    // center the UVs for rotation and zoom
    vec2 c = uv - 0.5;

    // zoom: pull toward center (or push away)
    float zoom = BASE_ZOOM + bass * BASS_ZOOM;
    c /= zoom;

    // rotate
    float rot = BASE_ROT + treb * TREB_ROT;
    float cs = cos(rot);
    float sn = sin(rot);
    c = vec2(c.x * cs - c.y * sn, c.x * sn + c.y * cs);

    // sinusoidal warp displacement
    vec2 warp = vec2(
        sin(c.y * WARP_SCALE * TWO_PI + time * WARP_SPEED),
        cos(c.x * WARP_SCALE * TWO_PI + time * WARP_SPEED * 0.7)
    );
    c += warp * WARP_AMOUNT * (1.0 + mid * 0.5);

    return c + 0.5;
}


// ===================== DRAW CONTENT ========================
// Draw new audio-reactive content on top of the warped previous
// frame. This is the main creative section. The default draws
// a waveform ring and a center pulse shape.
//
// Input:  existing = the warped and decayed previous frame
//         uv       = bottom-left UV coordinates
//         ndc      = aspect-corrected NDC coordinates (centered)
// Output: the composited color

vec3 drawWaveform(vec3 existing, vec2 ndc) {
    // draw the raw audio waveform as a ring around center
    float dist = length(ndc);
    float angle = atan(ndc.y, ndc.x);
    float t = (angle + PI) / TWO_PI;  // 0..1 around the circle

    // read the waveform at this angle
    int sampleIdx = clamp(int(t * float(hopSize)), 0, hopSize - 1);
    float samp = rawSamples[sampleIdx];

    // position the waveform on a ring
    float ringRadius = 0.3 + bass_att * 0.1;
    float wavePos = ringRadius + samp * 0.15;
    float waveDist = abs(dist - wavePos);
    float waveMask = smoothstep(WAVE_THICK, 0.0, waveDist);

    // color shifts with treb
    vec3 wc = WAVE_COLOR;
    wc = mix(wc, vec3(0.4, 0.7, 1.0), treb_att * 0.5);

    return mix(existing, wc * WAVE_BRIGHT, waveMask);
}

vec3 drawCenterShape(vec3 existing, vec2 ndc) {
    float dist = length(ndc);

    // pulsing circle driven by bass
    float radius = SHAPE_RADIUS + bass * SHAPE_PULSE;
    float shape = smoothstep(radius, radius - SHAPE_EDGE, dist);

    // color driven by bands
    vec3 shapeColor = vec3(
        0.5 + bass * 0.5,
        0.5 + mid * 0.5,
        0.5 + treb * 0.5
    );

    // brighter when louder than average
    float intensity = clamp(bass_rel * 0.5, 0.5, 0.5);
    shapeColor *= intensity;

    return mix(existing, shapeColor, shape * 0.8);
}

vec3 drawContent(vec3 existing, vec2 uv, vec2 ndc) {
    vec3 col = existing;
    col = drawWaveform(col, ndc);
    col = drawCenterShape(col, ndc);
    return col;
}


// ==================== POST-PROCESS =========================
// Final color adjustments applied to the composited frame.
// Add vignette, color grading, etc. here.

vec3 postProcess(vec3 col, vec2 uv) {
    // color tint
    col *= COLOR_TINT;

    // vignette
    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    // subtle brightness boost on transients
    float transient = clamp(bass_rel - 1.0, 0.0, 0.3);
    col += col * transient;

    return clamp(col, 0.0, 1.0);
}


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 px  = toPx();
    vec2 ndc = ndcBottomLeftAR();

    // 1. read audio bands and update tracking
    readBands();

    // 2. read previous frame through warp
    vec2 warpedUV = warpUV(uv);
    vec4 prev = readPixel(warpedUV);
    vec3 col = prev.rgb;

    // 3. decay: fade old content
    col *= DECAY;

    // 4. draw new content on top
    col = drawContent(col, uv, ndc);

    // 5. post-process
    col = postProcess(col, uv);

    // 6. write to feedback buffer and output
    writeBands();
    writePixel(vec4(col, 1.0), px);

    FragColor = vec4(col, 1.0);
}
