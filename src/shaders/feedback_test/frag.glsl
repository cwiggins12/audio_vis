uniform sampler2D noise_tex;

#define DB_MIN -80.0
#define DB_MAX   0.0

float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}

float rectOutline(vec2 px, vec2 lo, vec2 hi, float thickness) {
    bool insideX = px.x >= lo.x && px.x <= hi.x;
    bool insideY = px.y >= lo.y && px.y <= hi.y;
    if (!insideX || !insideY) return 0.0;
    bool onLeft   = px.x < lo.x + thickness;
    bool onRight  = px.x > hi.x - thickness;
    bool onBottom = px.y < lo.y + thickness;
    bool onTop    = px.y > hi.y - thickness;
    return (onLeft || onRight || onBottom || onTop) ? 1.0 : 0.0;
}

float peakLine(vec2 px, vec2 lo, vec2 hi, float t, float fromRight, float thickness) {
    bool insideX = px.x > lo.x + thickness && px.x < hi.x - thickness;
    bool insideY = px.y > lo.y + thickness && px.y < hi.y - thickness;
    if (!insideX || !insideY) return 0.0;

    float innerW = hi.x - lo.x - thickness * 2.0;
    float lineX;
    if (fromRight > 0.0) {
        lineX = hi.x - thickness - t * innerW;
    } else {
        lineX = lo.x + thickness + t * innerW;
    }
    return (abs(px.x - lineX) < 2.0) ? 1.0 : 0.0;
}

vec3 addPeakMeters(vec3 color, vec2 uv) {
    vec2 px = uv * vec2(W, H);

    float thick  = 8.0;
    float rW     = W * 0.20;
    float rH     = H * 0.15;
    float margin = 40.0;
    float drop   = H * 0.125;

    vec2 lLo = vec2(margin, H - margin - rH - drop);
    vec2 lHi = vec2(margin + rW, H - margin - drop);

    vec2 rLo = vec2(W - margin - rW, H - margin - rH - drop);
    vec2 rHi = vec2(W - margin, H - margin - drop);

    float lT = dbToT(peakRmsData[0]);
    float rT = dbToT(peakRmsData[2]);

    vec3 green = vec3(0.0, 1.0, 0.3);

    float mask =
        rectOutline(px, lLo, lHi, thick) +
        peakLine(px,   lLo, lHi, lT, 0.0, thick) +
        rectOutline(px, rLo, rHi, thick) +
        peakLine(px,   rLo, rHi, rT, 1.0, thick);

    return mix(color, green, clamp(mask, 0.0, 1.0));
}

vec3 addChannelNum(vec3 color, vec2 uv) {
    vec2 px = uv * vec2(W, H);
    float fontSize = 64.0;
    vec2  labelOrigin = vec2(100.0, H - 60.0 - fontSize);
    int   ch0 = 48; // '0'
    int   ch1 = 51; // '3'
    float label = renderCharRotated90(ch0, labelOrigin,                       fontSize, px)
                + renderCharRotated90(ch1, labelOrigin + vec2(fontSize + 4.0, 0.0), fontSize, px);
    vec3 green = vec3(0.0, 1.0, 0.3);
    return mix(color, green, clamp(label, 0.0, 1.0));
}

vec3 addFFTBars(vec3 color, vec2 uv) {
    vec2 px = uv * vec2(W, H);

    int   bars    = 32;
    float totalW  = W * 0.75;
    float startX  = (W - totalW) * 0.5;
    float gapX    = 4.0;
    float barW    = (totalW - gapX * float(bars + 1)) / float(bars);
    float bottomY = 20.0;
    float maxH    = H * 0.60;

    vec3  green   = vec3(0.0, 1.0, 0.3);
    float mask    = 0.0;

    for (int i = 0; i < bars; i++) {
        float barH = dbToT(fftData[i]) * maxH;

        float x0 = startX + gapX + float(i) * (barW + gapX);
        float x1 = x0 + barW;
        float y0 = bottomY;
        float y1 = bottomY + barH;

        bool inX = px.x >= x0 && px.x < x1;
        bool inY = px.y >= y0 && px.y < y1;
        if (inX && inY) mask = 1.0;
    }

    return mix(color, green, mask);
}

vec3 addScanning(vec3 color, vec2 uv) {
    vec2 px = uv * vec2(W, H);

    float cycle = mod(time, 2.0);
    if (cycle > 1.2) return color;

    float fontSize = 32.0;
    int totalChars = 11;

   int chars[128];
    chars[0]  = 83;  // S
    chars[1]  = 67;  // C
    chars[2]  = 65;  // A
    chars[3]  = 78;  // N
    chars[4]  = 78;  // N
    chars[5]  = 73;  // I
    chars[6]  = 78;  // N
    chars[7]  = 71;  // G
    chars[8]  = 46;  // .
    chars[9]  = 46;  // .
    chars[10] = 46;  // .

    float totalW = float(totalChars) * fontSize;
    vec2  origin = vec2((W - totalW) * 0.5, H - 30.0 - fontSize);

    float mask = renderText(chars, totalChars, origin, fontSize, px);

    vec3 green = vec3(0.0, 1.0, 0.3);
    return mix(color, green, clamp(mask, 0.0, 1.0));
}

float noise(vec2 p) {
    float s = texture(noise_tex, vec2(1.0, 2.0 * cos(time)) * time * 8.0 + p * 1.0).x;
    s *= s;
    return s;
}

float onOff(float a, float b, float c) {
    return step(c, sin(time + a * cos(time * b)));
}

float ramp(float y, float start, float end) {
    float inside = step(start, y) - step(end, y);
    float fact = (y - start) / (end - start) * inside;
    return (1.0 - fact) * inside;
}

float stripes(vec2 uv) {
    float noi = noise(uv * vec2(0.5, 1.0) + vec2(1.0, 3.0));
    return ramp(mod(uv.y * 4.0 + time / 2.0 + sin(time + sin(time * 0.63)), 1.0), 0.5, 0.6) * noi;
}

vec4 readFeedback(vec2 uv) {
    ivec2 px = ivec2(uv * vec2(W, H));
    px = clamp(px, ivec2(0), ivec2(int(W) - 1, int(H) - 1));
    int idx = (px.y * int(W) + px.x) * 4;
    return vec4(
      feedbackIn[idx + 0],
      feedbackIn[idx + 1],
      feedbackIn[idx + 2],
      feedbackIn[idx + 3]
    );
}

vec3 getVideo(vec2 uv) {
    vec2 look = uv;
    float window = 1.0 / (1.0 + 20.0 * (look.y - mod(time / 4.0, 1.0))
                                    * (look.y - mod(time / 4.0, 1.0)));
    look.x = look.x + sin(look.y * 10.0 + time) / 50.0
           * onOff(4.0, 4.0, 0.3) * (1.0 + cos(time * 80.0)) * window;
    float vShift = 0.4 * onOff(2.0, 3.0, 0.9)
               * (sin(time) * sin(time * 20.0)
               + (0.5 + 0.1 * sin(time * 200.0) * cos(time)));
    look.y = mod(look.y + vShift, 1.0);
    return readFeedback(look).rgb;
}

vec2 screenDistort(vec2 uvIn) {
    uvIn -= vec2(0.5, 0.5);
    uvIn = uvIn * 1.2 * (1.0 / 1.2 + 2.0 * uvIn.x * uvIn.x * uvIn.y * uvIn.y);
    uvIn += vec2(0.5, 0.5);
    return uvIn;
}

void writeFeedback(vec3 video, vec2 rawUV) {
    ivec2 px = ivec2(rawUV * vec2(W, H));
    px = clamp(px, ivec2(0), ivec2(int(W) - 1, int(H) - 1));
    int idx = (px.y * int(W) + px.x) * 4;
    feedbackOut[idx + 0] = video.r;
    feedbackOut[idx + 1] = video.g * 0.4;
    feedbackOut[idx + 2] = video.b * 0.4;
    feedbackOut[idx + 3] = 0.9;
}

void main() {
    vec2 rawUV = toPx() / vec2(W, H);
    vec2 uv = screenDistort(rawUV);

    vec3 video = getVideo(uv);

    float vigAmt = 3.0 + 0.3 * sin(time + 5.0 * cos(time * 5.0));
    float vignette = (1.0 - vigAmt * (uv.y - 0.5) * (uv.y - 0.5))
                 * (1.0 - vigAmt * (uv.x - 0.5) * (uv.x - 0.5));

    video += noise(uv * 2.0) / 2.0;
    video = addPeakMeters(video, rawUV);
    video = addFFTBars(video, rawUV);
    video = addScanning(video, rawUV);
    video = addChannelNum(video, rawUV);
    video += stripes(uv);
    video *= vignette;
    video *= (12.0 + mod(uv.y * 30.0 + time, 1.0)) / 13.0;
    video *= vec3(0.5, 1.0, 0.9);
    writeFeedback(video, rawUV);

    FragColor = vec4(video, 0.9);
}
