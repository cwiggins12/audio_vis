// ============================================================
// tunnel - frag.glsl
// ===========================================================
//
// Raymarched tunnel that curves and turns. The camera flies
// forward along a winding path. Bass drives speed, treble
// influences the path curvature, mid warps the tunnel walls.
//
// The tunnel path is defined by a function that returns the
// center position at any point along the z-axis. The path
// curves smoothly using sine waves at different frequencies.
//
// Uses normalizedFFT = true, so bass/mid/treb hover around 1.0
// ============================================================


const float PI      = 3.14159265359;
const float TWO_PI  = 6.28318530718;

// --- Speed ---
const float BASE_SPEED     = 1.0;
const float BASS_SPEED     = 4.0;

// --- Tunnel Geometry ---
const float TUNNEL_RADIUS  = 0.8;     // tunnel bore radius
const float CURVE_AMOUNT   = 1.2;     // how much the path curves
const float CURVE_FREQ     = 0.15;    // how often curves happen

// --- Wall Pattern ---
const float RING_COUNT     = 3.0;     // rings along depth
const float RING_THICK     = 0.35;
const int   SIDE_COUNT     = 8;       // angular segments
const float GRID_SHARP     = 0.88;

// --- Wall Warp ---
const float WARP_AMOUNT    = 0.06;
const float WARP_SPEED     = 0.6;

// --- Color ---
const float HUE_SPEED      = 0.04;
const float HUE_DEPTH_RATE = 0.08;
const float SATURATION     = 0.6;
const float VIGNETTE_AMT   = 0.4;

// --- Raymarch ---
const int   MAX_STEPS      = 60;
const float MAX_DIST       = 40.0;
const float HIT_DIST       = 0.01;

// --- Feedback ---
// [0] accumulated camera Z position
const int FB_CAM_Z = 0;


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


// ==================== TUNNEL PATH ==========================
// Returns the XY center of the tunnel at a given Z position.
// Multiple sine waves at incommensurate frequencies create a
// path that never exactly repeats.

vec2 tunnelPath(float z) {
    return vec2(
        sin(z * CURVE_FREQ) * CURVE_AMOUNT
            + sin(z * CURVE_FREQ * 2.3 + 1.0) * CURVE_AMOUNT * 0.4,
        cos(z * CURVE_FREQ * 0.7 + 2.0) * CURVE_AMOUNT * 0.7
            + sin(z * CURVE_FREQ * 1.7) * CURVE_AMOUNT * 0.3
    );
}


// ==================== DISTANCE FIELD =======================
// Signed distance to the tunnel wall. Negative = inside tunnel.

float tunnelSDF(vec3 p, float mid) {
    // get tunnel center at this Z
    vec2 center = tunnelPath(p.z);
    vec2 offset = p.xy - center;
    float dist = length(offset);

    // wall warp: mid deforms the radius
    float angle = atan(offset.y, offset.x);
    float warp = sin(angle * 4.0 + p.z * 2.0 + time * WARP_SPEED)
               * WARP_AMOUNT * mid;

    // distance to tunnel wall (negative inside, positive outside)
    return dist - TUNNEL_RADIUS - warp;
}


// ========================= MAIN ============================

void main() {
    vec2 uv  = uvBottomLeft();
    vec2 ndc = ndcBottomLeftAR();

    float bass = fftData[0];
    float mid  = fftData[1];
    float treb = fftData[2];

    // --- camera position ---
    float camZ = feedbackIn[FB_CAM_Z];
    float speed = BASE_SPEED + bass * bass * BASS_SPEED;
    camZ += speed * 0.016;
    feedbackOut[FB_CAM_Z] = camZ;

    // camera sits on the tunnel path
    vec2 camXY = tunnelPath(camZ);

    // look slightly ahead to get the forward direction
    float lookAhead = 0.5;
    vec2 camXYAhead = tunnelPath(camZ + lookAhead);
    vec3 camPos = vec3(camXY, camZ);

    // forward direction follows the path
    vec3 lookAt = vec3(camXYAhead, camZ + lookAhead);
    vec3 fwd = normalize(lookAt - camPos);

    // build camera basis vectors
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(fwd, worldUp));
    vec3 up = cross(right, fwd);

    // ray direction from screen coordinates
    vec3 rd = normalize(ndc.x * right + ndc.y * up + 1.5 * fwd);

    // --- raymarch ---
    float totalDist = 0.0;
    float minWallDist = 1e10;
    float hitZ = 0.0;
    bool hit = false;

    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = camPos + rd * totalDist;
        float d = tunnelSDF(p, mid);

        // track how close we get to the wall (for glow)
        minWallDist = min(minWallDist, abs(d));

        if (d > 0.0) {
            // we're outside the tunnel: hit the wall
            hitZ = p.z;
            hit = true;
            break;
        }

        totalDist += max(-d * 0.8, 0.02);

        if (totalDist > MAX_DIST) break;
    }

    vec3 col = vec3(0.0);

    if (hit) {
        vec3 hitPos = camPos + rd * totalDist;
        vec2 center = tunnelPath(hitPos.z);
        vec2 wallOffset = hitPos.xy - center;
        float wallAngle = atan(wallOffset.y, wallOffset.x);
        float wallDist = length(wallOffset);
        float depthAlongTunnel = hitPos.z;

        // === ring pattern ===
        float ringPhase = fract(depthAlongTunnel * RING_COUNT);
        float ringCenter = abs(ringPhase - 0.5) * 2.0;
        float ringWidth = RING_THICK * (0.7 + bass * 0.3);
        float ring = 1.0 - smoothstep(0.0, ringWidth, ringCenter);

        float ringEdge = smoothstep(ringWidth, ringWidth * 0.5, ringCenter);
        ringEdge *= smoothstep(0.0, 0.15, ringCenter);

        // === angular grid ===
        float angGrid = abs(sin(wallAngle * float(SIDE_COUNT)));
        angGrid = smoothstep(GRID_SHARP, 1.0, angGrid);

        // === depth grid ===
        float depthGrid = abs(sin(depthAlongTunnel * RING_COUNT * PI));
        depthGrid = smoothstep(GRID_SHARP, 1.0, depthGrid);

        float grid = max(angGrid, depthGrid);

        float pattern = ring * 0.55 + grid * 0.25 + ringEdge * 0.25;

        // === color ===
        float angularColor = sin(wallAngle * 2.0) * 0.06;
        float hue = fract(time * HUE_SPEED
                         + depthAlongTunnel * HUE_DEPTH_RATE
                         + angularColor);
        hue = fract(hue + (mid - 1.0) * 0.05);

        float sat = SATURATION;
        sat = clamp(sat + (treb - 1.0) * 0.15, 0.4, 1.0);

        // distance fog: further hits are dimmer
        float fog = exp(-totalDist * 0.08);

        float val = pattern * fog;
        val *= 0.5 + bass * 0.5;

        col = hsv2rgb(hue, sat, val);

        // ring edge highlights
        col += vec3(ringEdge * fog * 0.2 * bass);

        // grid glow
        col += hsv2rgb(fract(hue + 0.5), 0.5, grid * fog * 0.1 * treb);

    } else {
        // looking down the tunnel: far end glow
        float endGlow = exp(-totalDist * 0.15);
        float hue = fract(time * HUE_SPEED * 2.0);
        col = hsv2rgb(hue, 0.5, endGlow * 0.3 * bass);
    }

    // glow near walls even on miss rays (proximity glow)
    float wallGlow = exp(-minWallDist * 8.0) * 0.2;
    float glowHue = fract(time * HUE_SPEED + 0.33);
    col += hsv2rgb(glowHue, 0.6, wallGlow * bass);

    // vignette
    vec2 vc = uv - 0.5;
    float vignette = 1.0 - dot(vc, vc) * VIGNETTE_AMT * 4.0;
    col *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
