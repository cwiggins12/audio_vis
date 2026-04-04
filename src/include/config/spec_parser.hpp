#pragma once

#include "config/spec.hpp"
#include "config/expr_eval.hpp"
#include <fstream>
#include <string>

inline std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline std::string parseInterp(const std::string& val, int lineNum, Interps& out) {
    std::string ret = "";
    if (val == "LINEAR"        || val == "0") { out = LINEAR;        return ret; }
    if (val == "PCHIP"         || val == "1") { out = PCHIP;         return ret; }
    if (val == "LANCZOS"       || val == "2") { out = LANCZOS;       return ret; }
    if (val == "GAUSSIAN"      || val == "3") { out = GAUSSIAN;      return ret; }
    if (val == "CUBIC_B"       || val == "4") { out = CUBIC_B;       return ret; }
    if (val == "AKIMA"         || val == "5") { out = AKIMA;         return ret; }
    ret = "parseSpec: line " + std::to_string(lineNum) +
          ": invalid Interps value \"" + val + "\"\n";
    //std::cerr << ret;
    return ret;
}

inline std::string parseCollates(const std::string& val, int lineNum, Collates& out) {
    std::string ret = "";
    if (val == "RMS"        || val == "0") { out = RMS;        return ret; }
    if (val == "PEAK"       || val == "1") { out = PEAK;       return ret; }
    if (val == "POWER_MEAN" || val == "2") { out = POWER_MEAN; return ret; }
    ret = "parseSpec: line " + std::to_string(lineNum) +
          ": invalid Collates value \"" + val + "\"\n";
    //std::cerr << ret;
    return ret;
}

inline std::string parseFFTOutputMode(const std::string& val, int lineNum,
                                      FFTOutputMode& out) {
    std::string ret = "";
    if (val == "FULL_BIN"     || val == "0") { out = FULL_BIN;     return ret; }
    if (val == "AUDIBLE_BIN"  || val == "1") { out = AUDIBLE_BIN;  return ret; }
    if (val == "CUSTOM_SIZE"  || val == "2") { out = CUSTOM_SIZE;  return ret; }
    ret = "parseSpec: line " + std::to_string(lineNum) +
          ": invalid FFTOutputMode value \"" + val + "\"\n";
    //std::cerr << ret;
    return ret;
}

inline std::string parseWindowScalingMode(const std::string& val, int lineNum,
                                          WindowScalingMode& out) {
    std::string ret = "";
    if (val == "NO_SCALE"         || val == "0") { out = NO_SCALE;         return ret; }
    if (val == "WIDTH_SCALE"      || val == "1") { out = WIDTH_SCALE;      return ret; }
    if (val == "HEIGHT_SCALE"     || val == "2") { out = HEIGHT_SCALE;     return ret; }
    if (val == "RESOLUTION_SCALE" || val == "3") { out = RESOLUTION_SCALE; return ret; }
    ret = "parseSpec: line " + std::to_string(lineNum) +
          ": invalid WindowScalingMode value \"" + val + "\"\n";
    //std::cerr << ret;
    return ret;
}

inline std::string parseFFTMeasurement(const std::string& val, int lineNum,
                                       FFTMeasurement& out) {
    std::string ret = "";
    if (val == "POWER"     || val == "0") { out = POWER;     return ret; }
    if (val == "MAGNITUDE" || val == "1") { out = MAGNITUDE; return ret; }
    if (val == "DECIBELS"  || val == "2") { out = DECIBELS;  return ret; }
    ret = "parseSpec: line " + std::to_string(lineNum) +
          ": invalid FFTMeasurement value \"" + val + "\"\n";
    //std::cerr << ret;
    return ret;
}

inline std::string parseFloat(const std::string& val, int lineNum,
                               const std::string& key, float& out) {
    try {
        out = std::stof(val);
        return "";
    } catch (const std::invalid_argument&) {
        return "parseSpec: line " + std::to_string(lineNum) +
               ": invalid float value \"" + val + "\" for key \"" + key + "\"\n";
    } catch (const std::out_of_range&) {
        return "parseSpec: line " + std::to_string(lineNum) +
               ": float value out of range \"" + val + "\" for key \"" + key + "\"\n";
    }
}

inline std::string parseSpec(const std::string& path, Spec& out) {
    std::string ret = "";
    std::ifstream file(path);
    if (!file.is_open()) {
        ret = "parseSpec: could not open " + path + "\n";
        //std::cerr << ret;
        return ret;
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
            ret = "parseSpec: line " + std::to_string(lineNum) +
                  ": missing '=' in \"" + line + "\"\n";
            //std::cerr << ret;
            return ret;
        }

        std::string key = trimStr(line.substr(0, eq));
        std::string val = trimStr(line.substr(eq + 1));

        if (key.empty() || val.empty()) {
            ret = "parseSpec: line " + std::to_string(lineNum) +
                  ": empty key or value\n";
            //std::cerr << ret;
            return ret;
        }

        auto parseBool = [&](bool& field) -> bool {
            if (val == "true")  { field = true;  return true; }
            if (val == "false") { field = false; return true; }
            if (val == "1")     { field = true;  return true; }
            if (val == "0")     { field = false; return true; }
            ret = "parseSpec: line " + std::to_string(lineNum) +
                  ": expected true/false for \"" + key + "\"\n";
            //std::cerr << ret;
            return false;
        };

        if (key == "fftOutputMode") {
            if (parseFFTOutputMode(val, lineNum, out.fftOutputMode) != "") return ret;
        }
        else if (key == "customFFTSize") {
            out.customFFTSizeExpr = val;
            ExprContext ctx{};
            if (evalExpr(val, ctx, out.customFFTSize,
                         out.fftUsesExprVar, lineNum) != "") {
                return ret;
            }
        }
        else if (key == "customFFTSizeScalesWithWindow") {
            if (parseWindowScalingMode(val, lineNum,
                                       out.customFFTSizeScalesWithWindow)!= "") {
                return ret;
            }
        }
        else if (key == "highMode") {
            if (parseCollates(val, lineNum, out.highMode) != "") return ret;
        }
        else if (key == "lowMode") {
            if (parseInterp(val, lineNum, out.lowMode) != "") return ret;
        }
        else if (key == "fftOutputMeasurement") {
            if (parseFFTMeasurement(val, lineNum, out.fftOutputMeasurement) != "") {
                return ret;
            }
        }
        else if (key == "fftAtk") {
            if ((ret = parseFloat(val, lineNum, key, out.fftAtk)) != "") {
                return ret;
            }
        }
        else if (key == "fftRls") {
            if ((ret = parseFloat(val, lineNum, key, out.fftRls)) != "") {
                return ret;
            }
        }
        else if (key == "fftHoldTime") {
            if ((ret = parseFloat(val, lineNum, key, out.fftHoldTime)) != "") {
                return ret;
            }
        }
        else if (key == "fftHoldScalar") {
            if ((ret = parseFloat(val, lineNum, key, out.fftHoldScalar)) != "") {
                return ret;
            }
        }
        else if (key == "perceptualSlopeDegrees") {
            if ((ret = parseFloat(val, lineNum, key, 
                                  out.perceptualSlopeDegrees)) != "") {
                return ret;
            }
        }
        else if (key == "useFFTSmoothing") {
            if (!parseBool(out.useFFTSmoothing)) return ret;
        }
        else if (key == "getsFFTHolds") {
            if (!parseBool(out.getsFFTHolds)) return ret;
        }
        else if (key == "isFFTHannWindowed") {
            if (!parseBool(out.isFFTHannWindowed)) return ret;
        }
        else if (key == "usePeakRMSSmoothing") {
            if (!parseBool(out.usePeakRMSSmoothing)) return ret;
        }
        else if (key == "getsPeakRMSHolds") {
            if (!parseBool(out.getsPeakRMSHolds)) return ret;
        }
        else if (key == "isPeakRMSdB") {
            if (!parseBool(out.isPeakRMSdB)) return ret;
        }
        else if (key == "isPeakRMSMono") {
            if (!parseBool(out.isPeakRMSMono)) return ret;
        }
        else if (key == "peakRMSAtk") {
            if ((ret = parseFloat(val, lineNum, key, out.peakRMSAtk)) != "") {
                return ret;
            }
        }
        else if (key == "peakRMSRls") {
            if ((ret = parseFloat(val, lineNum, key, out.peakRMSRls)) != "") {
                return ret;
            }
        }
        else if (key == "peakRMSHoldTime") {
            if ((ret = parseFloat(val, lineNum, key, out.peakRMSHoldTime)) != "") {
                return ret;
            }
        }
        else if (key == "peakRMSHoldScalar") {
            if ((ret = parseFloat(val, lineNum, key, out.peakRMSHoldScalar)) != "") {
                return ret;
            }
        }
        else if (key == "feedbackBufferSize") {
            out.feedbackBufferSizeExpr = val;
            ExprContext ctx{};
            if (evalExpr(val, ctx, out.feedbackBufferSize,
                         out.feedbackUsesExprVar, lineNum) != "") {
                return ret;
            }
        }
        else if (key == "feedbackBufferScalesWithWindow") {
            if (parseWindowScalingMode(val, lineNum,
                                       out.feedbackBufferScalesWithWindow) != "") {
                return ret;
            }
        }
        else if (key == "feedbackBufferInitValue") {
            if ((ret = parseFloat(val, lineNum, key,
                                  out.feedbackBufferInitValue)) != "") {
                return ret;
            }
        }
        else if (key.rfind("texture.", 0) == 0) {
            std::string uniformName = trimStr(key.substr(8));
            if (uniformName.empty()) {
                ret = "parseSpec: line " + std::to_string(lineNum) +
                      ": empty texture uniform name\n";
                return ret;
            }
            out.textures[uniformName] = val;
        }
        else {
            ret = "parseSpec: line " + std::to_string(lineNum) +
                  ": unknown key \"" + key + "\"\n";
            return ret;
        }
    }
    return ret;
}

