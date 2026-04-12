//hi :)

vec4 helper(float waveY, vec2 px) {
    float dist = abs(px.y - waveY);
    float line = smoothstep(2.0, 0.0, dist);

    vec3 col;
    if (mouseDown == 1) {
        col = vec3(1.0);
    }
    else {
        col = vec3(mouseX / W, mouseY / H, 0.5);
    }
    return vec4(col * line, 1.0);
}

void main() {
    vec2 px = toPx();
    int x = int(px.x);

    float waveY;
    if (newAudioWindow == 1) {
        int cutoff = int(W) - hopSize;
        if (x >= cutoff) {
            float s = rawSamples[x - cutoff];
            waveY = (s * 0.5 + 0.5) * H;
        }
        else {
            waveY = feedbackIn[x + hopSize];
        }
    }
    else {
        waveY = feedbackIn[x];
    }

    feedbackOut[x] = waveY;

    FragColor = helper(waveY, px);
}
