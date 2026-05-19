// ============================================================
// polar tunnel - frag.glsl
// ============================================================
//
// Classic polar-coordinate tunnel effect with steering,
// wall warping, and dramatic audio-reactive speed.
//
// bass - speed (accumulated, bass^2 for dramatic range)
// treb - steering (tunnel curves via angular offset)
// mid  - wall warping (tunnel breathes and ripples)
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- Speed ---
const float BASE_SPEED     = 1.0;     // slow crawl when quiet
const float BASS_SPEED     = 4.0;     // bass^2 * this = huge range

// --- Steering ---
const float STEER_AMOUNT   = 0.3;
const float STEER_SMOOTH   = 0.12;

// --- Wall Warp ---
const float WARP_AMOUNT    = 0.00;
const float WARP_RINGS     = 3.0;
const float WARP_SIDES     = 4.0;
const float WARP_SPEED     = 0.5;

// --- Tunnel Shape ---
const float TUNNEL_TWIST   = 0.3;
const int   SIDE_COUNT     = 8;

// --- Pattern ---
const float RING_COUNT     = 6.0;
const float RING_THICK     = 0.3;
const float GRID_SHARP     = 0.88;

// --- Color ---
const float HUE_SPEED      = 0.05;
const float HUE_DEPTH_RATE = 0.35;
const float SATURATION     = 0.8;
const float VIGNETTE_AMT   = 0.45;

// --- Feedback layout ---
// [0] accumulated scroll
// [1] smoothed steer angle
const int FB_SCROLL = 0;
const int FB_STEER  = 1;


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


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 ndc = ndcBottomLeftAR();

    float bass = fftData[0];
    float mid  = fftData[1];
    float treb = fftData[2];

    // --- accumulated scroll ---
    float scroll = feedbackIn[FB_SCROLL];
    float speed = BASE_SPEED + bass * bass * BASS_SPEED;
    scroll += speed * 0.016;
    feedbackOut[FB_SCROLL] = scroll;

    // --- smoothed steering ---
    float steerTarget = (treb - 1.0) * STEER_AMOUNT;
    float steer = feedbackIn[FB_STEER];
    steer = mix(steerTarget, steer, 1.0 - STEER_SMOOTH);
    feedbackOut[FB_STEER] = steer;

    // --- polar mapping ---
    float angle = atan(ndc.y, ndc.x);
    float dist  = length(ndc);
    dist = max(dist, 0.001);
    float depth = 1.0 / dist;

    // --- wall warping ---
    float warpPhase = depth * WARP_RINGS + time * WARP_SPEED;
    float warpAng   = angle * WARP_SIDES;
    float warp = sin(warpPhase * TWO_PI + warpAng)
               * WARP_AMOUNT * mid;
    float warpedDist = dist + warp * dist;
    warpedDist = max(warpedDist, 0.001);
    float warpedDepth = 1.0 / warpedDist;

    // --- tunnel coordinates ---
    float v_coord = warpedDepth + scroll;

    // steering rotates angular coordinate with depth
    float steeredAngle = angle + steer * depth * 2.0;

    // twist — only used inside sin() for angular grid
    float twist = warpedDepth * TUNNEL_TWIST;

    // === ring pattern ===
    float ringPhase = fract(v_coord * RING_COUNT);
    float ringWidth = RING_THICK * (0.7 + bass * 0.3);

    float ringCenter = abs(ringPhase - 0.5) * 2.0;
    float ring = 1.0 - smoothstep(0.0, ringWidth, ringCenter);

    float ringEdge = smoothstep(ringWidth, ringWidth * 0.5, ringCenter);
    ringEdge *= smoothstep(0.0, 0.15, ringCenter);

    // === angular grid ===
    float angGrid = abs(sin((steeredAngle + twist) * float(SIDE_COUNT)));
    float gridThresh = GRID_SHARP;
    angGrid = smoothstep(gridThresh, 1.0, angGrid);

    // === depth grid ===
    float depthGrid = abs(sin(v_coord * RING_COUNT * PI));
    depthGrid = smoothstep(gridThresh, 1.0, depthGrid);

    float grid = max(angGrid, depthGrid);

    // === combine ===
    float pattern = ring * 0.55 + grid * 0.25 + ringEdge * 0.25;

    // === color ===
    // sin of angle for seamless angular color variation
    float angularColor = sin(steeredAngle * 2.0) * 0.08;
    float hue = fract(time * HUE_SPEED
                     + warpedDepth * HUE_DEPTH_RATE
                     + angularColor);
    hue = fract(hue + (mid - 1.0) * 0.12);

    float sat = SATURATION;
    sat = clamp(sat + (treb - 1.0) * 0.1, 0.4, 0.8);

    // depth fog
    float fog = clamp(dist * 2.5, 0.0, 1.0);
    float val = pattern * fog;

    // bass brightness
    val *= 0.5 + bass * 0.4;

    vec3 col = hsv2rgb(hue, sat, val);

    // ring edge highlights
    col += vec3(ringEdge * fog * 0.2 * bass);

    // grid glow in complementary color
    col += hsv2rgb(fract(hue + 0.5), 0.5, grid * fog * 0.1 * treb);

    // warp highlights
    float warpHighlight = clamp(-warp * 8.0, 0.0, 1.0) * fog;
    col += hsv2rgb(fract(hue + 0.33), 0.4, warpHighlight * 0.1 * mid);

    // center glow
    float centerGlow = smoothstep(0.12, 0.0, dist);
    col += hsv2rgb(fract(hue + 0.33), 0.4, centerGlow * 0.3 * bass);

    // vignette
    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
