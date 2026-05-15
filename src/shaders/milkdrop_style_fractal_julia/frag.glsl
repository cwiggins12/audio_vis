// ============================================================
// julia morph - frag.glsl
// ============================================================
//
// Full Julia set visible at all times, continuously morphing as
// the c-parameter orbits through parameter space. No zoom into
// the fractal plane — the visual interest comes from the set
// reshaping itself: filaments growing, splitting, connecting,
// spirals tightening and unwinding.
//
// Audio drives the orbit speed and path. Bass accelerates the
// orbit, treble perturbs the path, mid controls the view scale.
// Rotation is continuous and audio-reactive.
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


// ======================== CONSTANTS ========================

const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- C-Parameter Orbit ---
// The c-parameter traces a path through the complex plane.
// The most visually interesting Julia sets occur near the
// boundary of the Mandelbrot set, roughly |c| ~ 0.3 to 0.8.
// The orbit traces a warped loop through this region.
const float C_RADIUS_BASE   = 0.38;    // base orbit radius
const float C_RADIUS_VAR    = 0.30;    // how much the radius varies
const float C_RADIUS_SPEED  = 0.017;   // speed of radius variation
const float C_ORBIT_SPEED   = 0.05;    // base angular speed through c-space
const float C_BASS_ACCEL    = 0.04;    // bass accelerates the orbit
const float C_TREB_PERTURB  = 0.03;    // treble wobbles the orbit path

// --- View ---
// Fixed view scale — how much of the fractal plane is visible.
// 1.8 shows the full set with some margin.
const float VIEW_SCALE      = 1.8;
// Mid gently breathes the scale
const float VIEW_MID_SCALE  = 0.15;

// --- Rotation ---
const float BASE_ROT_RATE   = 0.006;
const float TREB_ROT_EXTRA  = 0.015;

// --- Iteration ---
const int   MAX_ITER        = 80;

// --- Color ---
const float COLOR_DENSITY   = 3.5;
const float COLOR_CYCLE     = 0.015;
const float COLOR_SAT       = 0.8;
const float INTERIOR_BRIGHT = 0.55;
const float EXTERIOR_BRIGHT = 0.95;
const vec3  COLOR_TINT      = vec3(1.0, 0.97, 0.95);
const float VIGNETTE_AMT    = 0.3;

// --- Band Tracking ---
const float ATT_RATE        = 0.95;
const float AVG_RATE        = 0.995;


// ================== FEEDBACK BUFFER LAYOUT =================
// [0..5]  : band tracking
// [6]     : accumulated c-orbit angle
// [7]     : accumulated view rotation

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


// ===================== JULIA RENDER ========================

vec3 renderJulia(vec2 ndc, float cAngle, float viewAngle) {
    // rotate the view
    float cs = cos(viewAngle);
    float sn = sin(viewAngle);
    vec2 rotated = vec2(ndc.x * cs - ndc.y * sn,
                        ndc.x * sn + ndc.y * cs);

    // scale to show the full set — mid breathes the view gently
    float scale = VIEW_SCALE + (mid_att - 1.0) * VIEW_MID_SCALE;
    vec2 z = rotated * scale;

    // compute c from the accumulated orbit angle
    // the radius varies sinusoidally so c traces a wobbly loop
    // that passes through different families of Julia sets
    float cRadius = C_RADIUS_BASE
                  + C_RADIUS_VAR * sin(cAngle * 0.23 + 1.0)
                  * cos(cAngle * 0.17);
    // treble perturbs the path off the clean orbit
    float cx = cRadius * cos(cAngle)
             + (treb_att - 1.0) * C_TREB_PERTURB * sin(cAngle * 3.0);
    float cy = cRadius * sin(cAngle)
             + (treb_att - 1.0) * C_TREB_PERTURB * cos(cAngle * 2.7);

    // orbit traps for interior coloring
    float trapOrigin = 1e10;
    float trapAxisX  = 1e10;
    float trapAxisY  = 1e10;
    float trapCircle = 1e10;

    // iterate z = z^2 + c
    float smoothIter = 0.0;
    bool escaped = false;
    for (int i = 0; i < MAX_ITER; i++) {
        z = vec2(z.x * z.x - z.y * z.y + cx,
                 2.0 * z.x * z.y + cy);

        float zLen = length(z);
        trapOrigin = min(trapOrigin, zLen);
        trapAxisX  = min(trapAxisX, abs(z.y));
        trapAxisY  = min(trapAxisY, abs(z.x));
        // circle trap at radius 1 — catches structure around unit circle
        trapCircle = min(trapCircle, abs(zLen - 1.0));

        if (dot(z, z) > 256.0) {
            smoothIter = float(i) - log2(log2(dot(z, z))) + 4.0;
            escaped = true;
            break;
        }
    }

    // normalize traps
    float t0 = clamp(trapOrigin * 0.5, 0.0, 1.0);
    float tx = clamp(trapAxisX * 2.0, 0.0, 1.0);
    float ty = clamp(trapAxisY * 2.0, 0.0, 1.0);
    float tc = clamp(1.0 - trapCircle * 3.0, 0.0, 1.0);
    float trapMix = t0 * 0.3 + tx * 0.2 + ty * 0.2 + tc * 0.3;

    float hue, sat, val;

    if (escaped) {
        float t = smoothIter / float(MAX_ITER);
        hue = fract(t * COLOR_DENSITY + cAngle * 0.05 + time * COLOR_CYCLE);
        sat = COLOR_SAT + t * 0.15;
        val = EXTERIOR_BRIGHT * (0.3 + t * 0.7);
    } else {
        hue = fract(trapMix * COLOR_DENSITY * 0.6 + cAngle * 0.05 + time * COLOR_CYCLE + 0.3);
        sat = COLOR_SAT * (0.5 + trapMix * 0.5);
        val = INTERIOR_BRIGHT * (0.2 + trapMix * 0.8);
    }

    // audio brightness
    val *= 0.85 + bass * 0.2;
    sat = clamp(sat + (mid - 1.0) * 0.08, 0.3, 1.0);

    return hsv2rgb(hue, sat, val);
}


// ==================== POST-PROCESS =========================

vec3 postProcess(vec3 col, vec2 uv) {
    col *= COLOR_TINT;

    // vignette
    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    // contrast
    col = smoothstep(0.0, 1.0, col);

    // bass transient flash
    float transient = clamp(bass - 1.3, 0.0, 0.3);
    col += col * transient;

    return clamp(col, 0.0, 1.0);
}


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 ndc = ndcBottomLeftAR();

    // 1. read audio
    readBands();

    // 2. read accumulated state
    float cAngle    = feedbackIn[FB_C_ANGLE];
    float viewAngle = feedbackIn[FB_ROTATION];

    // 3. advance c-parameter orbit — bass drives speed
    float orbitSpeed = C_ORBIT_SPEED + max(bass_att - 1.0, 0.0) * C_BASS_ACCEL;
    cAngle += orbitSpeed;

    // 4. advance view rotation — treble drives speed
    float rotSpeed = BASE_ROT_RATE + max(treb - 1.0, 0.0) * TREB_ROT_EXTRA;
    viewAngle += rotSpeed;

    // keep angles in range
    cAngle    = mod(cAngle, TWO_PI * 100.0);  // large range since radius uses non-integer multiples
    viewAngle = mod(viewAngle, TWO_PI);

    // 5. render
    vec3 col = renderJulia(ndc, cAngle, viewAngle);

    // 6. post-process
    col = postProcess(col, uv);

    // 7. write persistent state
    writeBands();
    feedbackOut[FB_C_ANGLE]  = cAngle;
    feedbackOut[FB_ROTATION] = viewAngle;

    FragColor = vec4(col, 1.0);
}
