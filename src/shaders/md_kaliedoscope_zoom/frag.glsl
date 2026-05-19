// ============================================================
// fractal kaleidoscope - frag.glsl
// ============================================================
//
// Kaleidoscope symmetry folding + Julia set iteration. The view
// is centered on the fractal boundary rather than the origin,
// so interesting detail fills the screen consistently.
//
// bass  → zoom pulsing, brightness
// mid   → fractal parameter drift speed, saturation
// treb  → rotation speed, color shift, edge glow
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- Kaleidoscope ---
const int   FOLD_COUNT      = 6;
const float FOLD_ANGLE      = PI / float(FOLD_COUNT);

// --- View ---
// viewScale controls how much fractal space the screen covers.
// smaller = more zoomed in on boundary detail
const float VIEW_SCALE      = 0.8;
const float VIEW_BREATHE    = 0.0;
const float VIEW_BREATHE_SPD= 0.0;
const float VIEW_BASS_PUSH  = 0.05;

// --- Rotation ---
const float BASE_ROT_SPEED  = -0.1;
const float TREB_ROT_SPEED  = -0.6;

// --- Fractal ---
const int   MAX_ITER        = 100;
const float C_DRIFT_SPEED   = 0.05;
const float C_MID_ACCEL     = 0.01;

// --- Screen Warp ---
// Sinusoidal displacement of screen coords before folding.
// Everything downstream (fold edges, fractal, ring, glow) warps together.
const float WARP_AMOUNT     = 0.06;    // base displacement strength
const float WARP_BASS_PUSH  = 0.08;    // bass adds more warp
const float WARP_SPEED      = 0.7;     // warp animation speed
const float WARP_RADIAL     = 3.0;     // radial frequency (rings of warp)
const float WARP_ANGULAR    = 2.0;     // angular frequency (lobes of warp)

// --- Color ---
const float COLOR_LAYERS    = 5.0;
const float COLOR_SPEED     = 0.03;
const float EDGE_GLOW       = 0.6;
const float VIGNETTE_AMT    = 0.25;

// --- Feedback ---
const int FB_ROTATION = 0;
const int FB_C_ANGLE  = 1;


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

vec2 kaleidoscope(vec2 p, int folds) {
    float a = atan(p.y, p.x);
    float foldAngle = TWO_PI / float(folds);
    a = mod(a, foldAngle);
    if (a > foldAngle * 0.5) {
        a = foldAngle - a;
    }
    float r = length(p);
    return vec2(cos(a), sin(a)) * r;
}

// Classic Julia set c-values that are known to produce rich detail.
// We interpolate between these to guarantee we're always near
// an interesting region of c-space.
vec2 getInterestingC(float t) {
    // 8 known-good c values arranged around c-space
    // each produces a visually rich Julia set
    vec2 c0 = vec2(-0.70, 0.27);   // spiral
    vec2 c1 = vec2(-0.54, 0.54);   // branching filaments
    vec2 c2 = vec2(0.355, 0.355);  // dendrite
    vec2 c3 = vec2(-0.4,  0.6);    // rabbit
    vec2 c4 = vec2(-0.75, 0.11);   // Douady rabbit
    vec2 c5 = vec2(0.28,  0.008);  // near main antenna
    vec2 c6 = vec2(-0.12, 0.74);   // Siegel disk boundary
    vec2 c7 = vec2(-1.25, 0.0);    // basilica

    // smoothly interpolate through all 8
    float segment = fract(t) * 8.0;
    int idx = int(floor(segment));
    float frac = fract(segment);
    // smooth interpolation
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


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 ndc = ndcBottomLeftAR();

    float bass = fftData[0];
    float mid  = fftData[1];
    float treb = fftData[2];

    // --- accumulated rotation ---
    float rotation = feedbackIn[FB_ROTATION];
    float rotSpeed = BASE_ROT_SPEED + max(treb - 1.0, 0.0) * TREB_ROT_SPEED;
    rotation += rotSpeed * 0.016;
    rotation = mod(rotation, TWO_PI);
    feedbackOut[FB_ROTATION] = rotation;

    // --- accumulated c-parameter progression ---
    float cAngle = feedbackIn[FB_C_ANGLE];
    float cSpeed = C_DRIFT_SPEED + max(mid - 1.0, 0.0) * C_MID_ACCEL;
    cAngle += cSpeed * 0.016;
    feedbackOut[FB_C_ANGLE] = cAngle;

    // --- rotate the view ---
    float cs = cos(rotation);
    float sn = sin(rotation);
    vec2 p = vec2(ndc.x * cs - ndc.y * sn,
                  ndc.x * sn + ndc.y * cs);

    // --- screen warp ---
    // sinusoidal displacement in polar coords — radial and angular
    // waves create rippling distortion that pulses with audio.
    // applied before folding so fold edges, fractal, ring, and
    // glow all warp together coherently.
    float pDist = length(p);
    float pAngle = atan(p.y, p.x);
    float warpStr = WARP_AMOUNT + max(bass - 1.0, 0.0) * WARP_BASS_PUSH;
    // radial wave: rings of displacement expanding from center
    float radialWave = sin(pDist * WARP_RADIAL * TWO_PI - time * WARP_SPEED);
    // angular wave: lobes around the circle
    float angularWave = cos(pAngle * WARP_ANGULAR + time * WARP_SPEED * 0.6);
    // combine into a displacement vector (push radially)
    float warpDisp = radialWave * 0.6 + angularWave * 0.4;
    warpDisp *= warpStr * pDist;  // scale with distance so center stays stable
    // mid modulates the angular component
    warpDisp += sin(pAngle * 3.0 + time * WARP_SPEED * 1.3)
              * max(mid - 1.0, 0.0) * 0.04 * pDist;
    // apply as radial displacement
    vec2 warpDir = (pDist > 0.001) ? p / pDist : vec2(0.0);
    p += warpDir * warpDisp;

    float distFromCenter = length(p);

    // --- kaleidoscope fold ---
    vec2 folded = kaleidoscope(p, FOLD_COUNT);

    // --- get c from the curated path ---
    // cAngle is accumulated time, divided to cycle through all 8
    // c-values over a period. bass adds a small perturbation.
    float cProgress = cAngle * 0.1;
    vec2 c = getInterestingC(cProgress);
    c.x += (bass - 1.0) * 0.015;
    c.y += (treb - 1.0) * 0.01;
    float cx = c.x;
    float cy = c.y;

    // --- view window centered on the fractal boundary ---
    // instead of z = folded * zoom (centered on origin),
    // offset so screen center maps to a point ON the boundary.
    // the boundary of a Julia set passes through critical points
    // related to c. Centering near c/2 usually puts boundary
    // detail in view.
    float breathe = sin(time * VIEW_BREATHE_SPD) * VIEW_BREATHE;
    float scale = VIEW_SCALE + breathe + (bass - 1.0) * VIEW_BASS_PUSH;
    scale = max(scale, 0.3);

    vec2 viewCenter = c * 0.4;
    vec2 z = folded * scale + viewCenter;

    // --- iterate ---
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

    // --- color ---
    float t0 = clamp(trapOrigin * 0.4, 0.0, 1.0);
    float tc = clamp(1.0 - trapCircle * 2.5, 0.0, 1.0);
    float tl = clamp(1.0 - trapLine * 0.5, 0.0, 1.0);
    float trapMix = t0 * 0.3 + tc * 0.35 + tl * 0.35;

    float hue, sat, val;

    if (escaped) {
        float t = smoothIter / float(MAX_ITER);
        hue = fract(t * COLOR_LAYERS + time * COLOR_SPEED + cProgress * 0.2);
        sat = 0.65 + t * 0.25;
        val = 0.35 + t * 0.65;
    } else {
        hue = fract(trapMix * COLOR_LAYERS * 0.5 + time * COLOR_SPEED + 0.3 + cProgress * 0.2);
        sat = 0.5 + trapMix * 0.45;
        val = 0.15 + trapMix * 0.7;
    }

    // treb shifts hue
    hue = fract(hue + max(treb - 1.0, 0.0) * 0.2);
    // mid drives saturation
    sat = clamp(sat + (mid - 1.0) * 0.15, 0.3, 1.0);
    // bass drives brightness
    val *= 0.7 + bass * 0.3;

    vec3 col = hsv2rgb(hue, sat, val);

    // --- edge glow at fold boundaries ---
    float foldA = atan(p.y, p.x);
    float foldAngle = TWO_PI / float(FOLD_COUNT);
    float nearFold = abs(mod(foldA + foldAngle * 0.5, foldAngle) - foldAngle * 0.5);
    float edgeMask = smoothstep(0.04, 0.0, nearFold) * EDGE_GLOW;
    edgeMask *= clamp(distFromCenter * 3.0, 0.0, 1.0);
    edgeMask *= 0.5 + treb * 0.5;
    col += hsv2rgb(fract(hue + 0.5), 0.4, edgeMask);

    // --- waveform ring ---
    float waveAngle = atan(ndc.y, ndc.x);
    float waveT = (waveAngle + PI) / TWO_PI;
    int sampleIdx = clamp(int(waveT * float(hopSize)), 0, hopSize - 1);
    float samp = rawSamples[sampleIdx];
    float ringRadius = 0.8 + (bass - 1.0) * 0.06;
    float wavePos = ringRadius + samp * 0.7;
    float waveDist = abs(distFromCenter - wavePos);
    float waveMask = smoothstep(0.02, 0.0, waveDist);
    vec3 waveColor = hsv2rgb(fract(hue + 0.15), mid * 0.5, 1.0);
    col = mix(col, waveColor, waveMask * 0.8);

    // --- center glow ---
    float centerGlow = smoothstep(0.15, 0.0, distFromCenter);
    col += hsv2rgb(fract(hue + 0.33), 0.3, centerGlow * 0.25 * bass);

    // treb flash
    float trebFlash = clamp((treb - 1.3) * 1.5, 0.0, 0.2);
    col += vec3(trebFlash);

    // vignette
    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
