#pragma once

#include "audio_spec.hpp"
#include "expr_eval.hpp"
#include <fstream>
#include <string>
#include <iostream>

inline std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline bool parseInterp(const std::string& val, int lineNum, Interps& out) {
    if (val == "LINEAR"        || val == "0") { out = LINEAR;        return true; }
    if (val == "PCHIP"         || val == "1") { out = PCHIP;         return true; }
    if (val == "LANCZOS"       || val == "2") { out = LANCZOS;       return true; }
    if (val == "GAUSSIAN"      || val == "3") { out = GAUSSIAN;      return true; }
    if (val == "CUBIC_B"       || val == "4") { out = CUBIC_B;       return true; }
    if (val == "AKIMA"         || val == "5") { out = AKIMA;         return true; }
    if (val == "STEFFEN"       || val == "6") { out = STEFFEN;       return true; }
    if (val == "CATMULL_ROM_3" || val == "7") { out = CATMULL_ROM_3; return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid Interps value \"" << val << "\"\n";
    return false;
}

inline bool parseCollates(const std::string& val, int lineNum, Collates& out) {
    if (val == "RMS"        || val == "0") { out = RMS;        return true; }
    if (val == "PEAK"       || val == "1") { out = PEAK;       return true; }
    if (val == "POWER_MEAN" || val == "2") { out = POWER_MEAN; return true; }
    if (val == "L_NORM"     || val == "3") { out = L_NORM;     return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid Collates value \"" << val << "\"\n";
    return false;
}

inline bool parseFFTOutputMode(const std::string& val, int lineNum, FFTOutputMode& out) {
    if (val == "FULL_BIN"     || val == "0") { out = FULL_BIN;     return true; }
    if (val == "AUDIBLE_BIN"  || val == "1") { out = AUDIBLE_BIN;  return true; }
    if (val == "CUSTOM_SIZE"  || val == "2") { out = CUSTOM_SIZE;  return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid FFTOutputMode value \"" << val << "\"\n";
    return false;
}

inline bool parseWindowScalingMode(const std::string& val, int lineNum, WindowScalingMode& out) {
    if (val == "NO_SCALE"         || val == "0") { out = NO_SCALE;         return true; }
    if (val == "WIDTH_SCALE"      || val == "1") { out = WIDTH_SCALE;      return true; }
    if (val == "HEIGHT_SCALE"     || val == "2") { out = HEIGHT_SCALE;     return true; }
    if (val == "RESOLUTION_SCALE" || val == "3") { out = RESOLUTION_SCALE; return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid WindowScalingMode value \"" << val << "\"\n";
    return false;
}

inline bool parseSecondPassMode(const std::string& val, int lineNum, SecondPassMode& out) {
    if (val == "NO_SECOND_PASS"      || val == "0") { out = NO_SECOND_PASS;      return true; }
    if (val == "USE_LOW_END_INTERP"  || val == "1") { out = USE_LOW_END_INTERP;  return true; }
    if (val == "USE_SEPARATE_INTERP" || val == "2") { out = USE_SEPARATE_INTERP; return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid SecondPassMode value \"" << val << "\"\n";
    return false;
}

inline bool parseFFTMeasurement(const std::string& val, int lineNum, FFTMeasurement& out) {
    if (val == "POWER"     || val == "0") { out = POWER;     return true; }
    if (val == "MAGNITUDE" || val == "1") { out = MAGNITUDE; return true; }
    if (val == "DECIBELS"  || val == "2") { out = DECIBELS;  return true; }
    std::cerr << "parseSpec: line " << lineNum
              << ": invalid FFTMeasurement value \"" << val << "\"\n";
    return false;
}

inline bool parseSpec(const std::string& path, Spec& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "parseSpec: could not open " << path << "\n";
        return false;
    }

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;

        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);

        line = trimStr(line);
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << "parseSpec: line " << lineNum
                      << ": missing '=' in \"" << line << "\"\n";
            return false;
        }

        std::string key = trimStr(line.substr(0, eq));
        std::string val = trimStr(line.substr(eq + 1));

        if (key.empty() || val.empty()) {
            std::cerr << "parseSpec: line " << lineNum
                      << ": empty key or value\n";
            return false;
        }

        auto parseBool = [&](bool& field) -> bool {
            if (val == "true")  { field = true;  return true; }
            if (val == "false") { field = false; return true; }
            std::cerr << "parseSpec: line " << lineNum
                      << ": expected true/false for \"" << key << "\"\n";
            return false;
        };

        if (key == "fftOutputMode") {
            if (!parseFFTOutputMode(val, lineNum, out.fftOutputMode)) return false;
        }
        else if (key == "customFFTSize") {
            out.customFFTSizeExpr = val;
            ExprContext ctx{};
            if (!evalExpr(val, ctx, out.customFFTSize, lineNum)) return false;
        }
        else if (key == "customFFTSizeScalesWithWindow") {
            if (!parseWindowScalingMode(val, lineNum, out.customFFTSizeScalesWithWindow)) return false;
        }
        else if (key == "highMode") {
            if (!parseCollates(val, lineNum, out.highMode)) return false;
        }
        else if (key == "lowMode") {
            if (!parseInterp(val, lineNum, out.lowMode)) return false;
        }
        else if (key == "highSecondPassMode") {
            if (!parseSecondPassMode(val, lineNum, out.highSecondPassMode)) return false;
        }
        else if (key == "highSecondPassInterp") {
            if (!parseInterp(val, lineNum, out.highSecondPassInterp)) return false;
        }
        else if (key == "fftOutputMeasurement") {
            if (!parseFFTMeasurement(val, lineNum, out.fftOutputMeasurement)) return false;
        }
        else if (key == "fftAtk") {
            out.fftAtk = std::stof(val);
        }
        else if (key == "fftRls") {
            out.fftRls = std::stof(val);
        }
        else if (key == "fftHoldTime") {
            out.fftHoldTime = std::stof(val);
        }
        else if (key == "fftHoldScalar") {
            out.fftHoldScalar = std::stof(val);
        }
        else if (key == "perceptualSlopeDegrees") {
            out.perceptualSlopeDegrees = std::stof(val);
        }
        else if (key == "useFFTSmoothing") {
            if (!parseBool(out.useFFTSmoothing)) return false;
        }
        else if (key == "getsFFTHolds") {
            if (!parseBool(out.getsFFTHolds)) return false;
        }
        else if (key == "isFFTHannWindowed") {
            if (!parseBool(out.isFFTHannWindowed)) return false;
        }
        else if (key == "usePeakRMSSmoothing") {
            if (!parseBool(out.usePeakRMSSmoothing)) return false;
        }
        else if (key == "getsPeakRMSHolds") {
            if (!parseBool(out.getsPeakRMSHolds)) return false;
        }
        else if (key == "isPeakRMSdB") {
            if (!parseBool(out.isPeakRMSdB)) return false;
        }
        else if (key == "isPeakRMSMono") {
            if (!parseBool(out.isPeakRMSMono)) return false;
        }
        else if (key == "peakRMSAtk") {
            out.peakRMSAtk = std::stof(val);
        }
        else if (key == "peakRMSRls") {
            out.peakRMSRls = std::stof(val);
        }
        else if (key == "peakRMSHoldTime") {
            out.peakRMSHoldTime = std::stof(val);
        }
        else if (key == "peakRMSHoldScalar") {
            out.peakRMSHoldScalar = std::stof(val);
        }
        else if (key == "feedbackBufferSize") {
            out.feedbackBufferSizeExpr = val;
            ExprContext ctx{};
            if (!evalExpr(val, ctx, out.feedbackBufferSize, lineNum)) return false;
        }
        else if (key == "feedbackBufferScalesWithWindow") {
            if (!parseWindowScalingMode(val, lineNum, out.feedbackBufferScalesWithWindow)) return false;
        }
        else if (key == "feedbackBufferInitValue") {
            out.feedbackBufferInitValue = std::stof(val);
        }
        else if (key.rfind("texture.", 0) == 0) {
            std::string uniformName = trimStr(key.substr(8));
            if (uniformName.empty()) {
                std::cerr << "parseSpec: line " << lineNum
                          << ": empty texture uniform name\n";
                return false;
            }
            out.textures[uniformName] = val;
        }
        else {
            std::cerr << "parseSpec: line " << lineNum
                      << ": unknown key \"" << key << "\"\n";
            return false;
        }
    }
    return true;
}
