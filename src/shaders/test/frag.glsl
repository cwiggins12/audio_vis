const float DB_MIN  = -96.0;
const float DB_MAX  =   0.0;
const float OUTLINE =   2.0;
const vec4 COL_FFT  = vec4(0.0, 1.0, 0.0, 1.0);
const vec4 COL_PEAK = vec4(1.0, 1.0, 0.0, 1.0);
const vec4 COL_RMS  = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 COL_HOLD = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_OUT  = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 COL_BG   = vec4(0.0, 0.0, 0.0, 1.0);
float dbToT(float db) {
    return clamp((db - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
}
bool inRange(float px, float lo, float hi) {
    return px >= lo && px < hi;
}
void drawMeter(float localX, float localY, float fillDb, float holdDb,
               float width, float measH, float holdH, vec4 fillCol, inout vec4 color) {
    float innerH = measH - 2.0 * OUTLINE;
    if (localX < OUTLINE || localX >= width - OUTLINE ||
        localY < OUTLINE || localY >= measH - OUTLINE) {
        color = COL_OUT;
    } else {
        float innerY = localY - OUTLINE;
        float fillT  = dbToT(fillDb) * innerH;
        float holdT  = dbToT(holdDb) * innerH;
        if (innerY >= holdT - holdH && innerY < holdT) {
            color = COL_HOLD;
        } else if (innerY < fillT) {
            color = fillCol;
        }
    }
}
void main() {
    float scaleX = W / 1280.0;
    float scaleY = H / 720.0;

    float sp       = 40.0  * scaleX;
    float hsp      = 20.0  * scaleX;
    float fft_w    = 1000.0 * scaleX;
    float half_mtr = 35.0  * scaleX;
    float meas_h   = 640.0 * scaleY;
    float hold_h   =   4.0 * scaleY;

    float fft_x0   = sp;
    float fft_x1   = sp + fft_w;
    float lpeak_x0 = fft_x1 + sp;
    float lpeak_x1 = lpeak_x0 + half_mtr;
    float lrms_x0  = lpeak_x1;
    float lrms_x1  = lrms_x0 + half_mtr;
    float rpeak_x0 = lrms_x1 + hsp;
    float rpeak_x1 = rpeak_x0 + half_mtr;
    float rrms_x0  = rpeak_x1;
    float rrms_x1  = rrms_x0 + half_mtr;
    float meas_y0  = sp;
    float meas_y1  = sp + meas_h;

    vec2 p = toPx();
    float px = p.x;
    float py = p.y;
    vec4 color = COL_BG;

    // FFT
    if (inRange(px, fft_x0, fft_x1) && inRange(py, meas_y0, meas_y1)) {
        float localX = px - fft_x0;
        float localY = py - meas_y0;
        float innerH = meas_h - 2.0 * OUTLINE;
        if (localX < OUTLINE || localX >= fft_w - OUTLINE ||
            localY < OUTLINE || localY >= meas_h - OUTLINE) {
            color = COL_OUT;
        } else {
            int bin      = clamp(int((localX - OUTLINE) / (fft_w - 2.0 * OUTLINE) * float(numBins)), 0, numBins - 1);
            float innerY = localY - OUTLINE;
            float fftDb  = fftData[bin];
            float holdDb = fftHolds[bin];
            float fftT   = dbToT(fftDb)  * innerH;
            float holdT  = dbToT(holdDb) * innerH;
            if (innerY >= holdT - hold_h && innerY < holdT) {
                color = COL_HOLD;
            } else if (innerY < fftT) {
                color = COL_FFT;
            }
        }
    }

    // L Peak
    if (inRange(px, lpeak_x0, lpeak_x1) && inRange(py, meas_y0, meas_y1))
        drawMeter(px - lpeak_x0, py - meas_y0, peakRmsData[0], prHolds[0], half_mtr, meas_h, hold_h, COL_PEAK, color);
    // L RMS
    if (inRange(px, lrms_x0, lrms_x1) && inRange(py, meas_y0, meas_y1))
        drawMeter(px - lrms_x0, py - meas_y0, peakRmsData[1], prHolds[1], half_mtr, meas_h, hold_h, COL_RMS, color);
    // R Peak
    if (inRange(px, rpeak_x0, rpeak_x1) && inRange(py, meas_y0, meas_y1))
        drawMeter(px - rpeak_x0, py - meas_y0, peakRmsData[2], prHolds[2], half_mtr, meas_h, hold_h, COL_PEAK, color);
    // R RMS
    if (inRange(px, rrms_x0, rrms_x1) && inRange(py, meas_y0, meas_y1))
        drawMeter(px - rrms_x0, py - meas_y0, peakRmsData[3], prHolds[3], half_mtr, meas_h, hold_h, COL_RMS, color);

    FragColor = color;
}
