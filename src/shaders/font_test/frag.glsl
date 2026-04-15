uniform sampler2D font;
uniform sampler2D fontMetrics;

const float DB_MIN = -80.0;
const float DB_MAX = 0.0;

float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}

float analyserBottom(vec2 px, float db, float baseY, float height) {
    float norm = clamp((db + 80.0) / 80.0, 0.0, 1.0);
    float lineY = baseY + norm * height;
    return step(px.y, lineY);
}

float analyserTop(vec2 px, float db, float baseY, float height) {
    float norm = clamp((db + 80.0) / 80.0, 0.0, 1.0);
    float lineY = baseY + (1.0 - norm) * height;
    return step(lineY, px.y);
}

int xToBin(float x, float totalW, int binCount) {
    return clamp(int(x / totalW * float(binCount)), 0, binCount - 1);
}

// t goes 0 to 1 across the x axis
vec3 bottomColor(float t) {
    vec3 left  = vec3(0.2, 0.6, 1.0);
    vec3 right = vec3(0.0, 1.0, 0.4);
    return mix(left, right, t);
}

vec3 topColor(float t) {
    vec3 left  = vec3(1.0, 0.2, 0.4);
    vec3 right = vec3(1.0, 0.8, 0.0);
    return mix(left, right, t);
}

void main() {
    vec2 px = toPx();
    int fragX = int(px.x);
    int fragY = int(px.y);

    if (fragY == 0 && fragX < fftArrSize) {
        feedbackOut[fragX] = fftData[fragX];
    }
	float rmsDB = dbToT(peakRmsData[0]);
    vec3 textCol = vec3(rmsDB, 0.0, abs(sin(time)));
    vec3 bgCol   = vec3(0.0);
    float t      = px.x / W;

    // text
    int chars[128];
    chars[0] = 70;  chars[1] = 111; chars[2] = 110; chars[3] = 116;
    chars[4] = 32;  chars[5] = 69;  chars[6] = 120; chars[7] = 97;
    chars[8] = 109; chars[9] = 112; chars[10] = 108; chars[11] = 101;
    chars[12] = 32; chars[13] = 58; chars[14] = 41;
    vec2 origin = vec2(W / 2.0 - 6.5 * 64.0, H / 2.0 - 64.0);
    float text = renderSdfText(font, fontMetrics,
                               chars, 15, origin, 128.0,
                               px, 0, 32, 95);

    // bottom analyser
    float halfH = H / 2.0;
    float bottom = 0.0;
    if (px.y < halfH) {
        int bin = xToBin(px.x, W, fftArrSize);
        bottom = analyserBottom(px, fftData[bin], 0.0, halfH);
    }

    // top analyser
    float top = 0.0;
    if (px.y >= halfH) {
        int bin = xToBin(W - px.x, W, fftArrSize);
        top = analyserTop(px, feedbackIn[bin], halfH, halfH);
    }

    // composite
    vec3 col = bgCol;
    col = mix(col, bottomColor(t), bottom);
    col = mix(col, topColor(t), top);
    col = mix(col, textCol, text);
    FragColor = vec4(col, 1.0);
}
