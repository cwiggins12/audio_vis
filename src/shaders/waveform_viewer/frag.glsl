//at each new audio window, get new samples and have a waveform come out of the left side of the screen, to do this: write all fb buffer in samples at index - hopSize to the window save them to fbBufferOut, incoming range will be -1 to 1, map this to h.
//at no new window, write in and put in out
//make this also test mouse buttons

void main() {
    vec2 px = toPx();
    int x = int(px.x);

    float waveY;
    if (newAudioWindow == 1) {
        if (x >= hopSize) {
            waveY = feedbackIn[x - hopSize];
        }
        else {
            float s = rawSamples[x];
            waveY = (s * 0.5 + 0.5) * H;
        }
    }
    else {
        waveY = feedbackIn[x];
    }

    feedbackOut[x] = waveY;

    float dist = abs(px.y - waveY);
    float line = smoothstep(2.0, 0.0, dist);

    vec3 col;
    if (mouseDown = 1) {
        col = vec(1.0);
    }
    else {
        col = vec3(mouseX / W, mouseY / H, 0.5);
    }
    FragColor = vec4(col * line, 1.0);
}
