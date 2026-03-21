const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float RING_RADIUS = 0.28;
const float RING_WIDTH  = 0.18;
const float DB_MIN = -96.0;
const float DB_MAX =   0.0;

float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}

void main() {
    // normalized centered coords
    vec2 centered = (uv - 0.5) * vec2(W / H, 1.0);
    float dist  = length(centered);
    float angle = atan(centered.y, centered.x);

    // map angle to bin index
    float t       = (angle / TWO_PI) + 0.5;
    int   bin     = clamp(int(t * float(numBins)), 0, numBins - 1);
    float binVal  = dbToT(fftData[bin]);

    // ring: inner edge is RING_RADIUS, outer edge expands with bin value
    float inner   = RING_RADIUS;
    float outer   = RING_RADIUS + binVal * RING_WIDTH;
    float inRing  = step(inner, dist) * step(dist, outer);

    // color shifts from deep blue at quiet to cyan/white at loud
    vec3 ringCol  = mix(vec3(0.0, 0.2, 0.8),
                        vec3(0.2, 1.0, 0.9),
                        binVal);

    // center pulse driven by mono RMS (index 1 = RMS in mono layout)
    float rms      = dbToT(peakRmsData[1]);
    float pulse    = step(dist, 0.08 + rms * 0.06);
    vec3  pulseCol = mix(vec3(0.0), vec3(1.0, 0.4, 0.1), rms);

    // subtle radial glow behind the ring
    float glow    = smoothstep(RING_RADIUS + RING_WIDTH * 0.5,
                               RING_RADIUS - 0.05, dist)
                  * smoothstep(RING_RADIUS - 0.15, RING_RADIUS, dist)
                  * 0.25;
    vec3 glowCol  = vec3(0.0, 0.3, 0.6) * glow;

    vec3 col = glowCol;
    col = mix(col, ringCol,  inRing);
    col = mix(col, pulseCol, pulse);

    FragColor = vec4(col, 1.0);
}
