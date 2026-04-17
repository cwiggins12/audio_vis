//hi :)

void main() {
    vec2 px = toPx();
    int x = int(px.x);
    int iW = int(W);
    float nCh = float(numChannels);
    float chH = H / nCh;
    int channel = int(px.y / chH);

    int fbIdx = x * numChannels + channel;

    float waveY;
    if (newAudioWindow == 1) {
        int cutoff = iW - hopSize;
        if (x >= cutoff) {
            int frame = x - cutoff;
            float s = rawSamples[frame * numChannels + channel];
            waveY = (s * 0.5 + 0.5) * chH;
        }
        else {
            waveY = feedbackIn[(x + hopSize) * numChannels + channel];
        }
    }
    else {
        waveY = feedbackIn[fbIdx];
    }

    feedbackOut[fbIdx] = waveY;

    float localY = px.y - float(channel) * chH;
    float dist = abs(localY - waveY);
    float line = smoothstep(2.0, 0.0, dist);

    vec3 col;
    float g = (channel == 0) ? abs(sin(time)) : abs(cos(time));
    g = clamp(g, 0.2, 1.0);
    if (mouseDown == 1) {
        col = vec3(1.0);
    }
    else {
        col = vec3(mouseX / W, g, mouseY / H);
    }
    FragColor = vec4(col * line, 1.0);
}
