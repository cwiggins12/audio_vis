#pragma once

#include "audio_spec.hpp"
#include <fstream>
#include <string>
#include <iostream>

inline std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline bool parseSpec(const std::string& path, AudioSpec& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "parseSpec: could not open " << path << std::endl;
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;

        //strip comments
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

        //bool helper
        auto parseBool = [&](bool& field) -> bool {
            if (val == "true")  { field = true;  return true; }
            if (val == "false") { field = false; return true; }
            std::cerr << "parseSpec: line " << lineNum
                      << ": expected true/false for \"" << key << "\"\n";
            return false;
        };

        //match every AudioSpec field
        if (key == "customLinearSize") {
            out.customLinearSize = std::stoul(val);
        }
        else if (key == "fftAtk") {
            out.fftAtk = std::stof(val);
        }
        else if (key == "fftRls") {
            out.fftRls = std::stof(val);
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
        else if (key == "fftHoldTime") {
            out.fftHoldTime = std::stof(val);
        }
        else if (key == "peakRMSHoldScalar") {
            out.peakRMSHoldScalar = std::stof(val);
        }
        else if (key == "fftHoldScalar") {
            out.fftHoldScalar = std::stof(val);
        }
        else if (key == "perceptualSlopeDegrees") {
            out.perceptualSlopeDegrees = std::stof(val);
        }
        else if (key == "isSizeWidthDependent") {
            if (!parseBool(out.isSizeWidthDependent)) {
                return false;
            }
        }
        else if (key == "isSizeHeightDependent") {
            if (!parseBool(out.isSizeHeightDependent)) {
                return false;
            }
        }
        else if (key == "useFFTSmoothing") {
            if (!parseBool(out.useFFTSmoothing)) {
                return false;
            }
        }
        else if (key == "useAudibleSize") {
            if (!parseBool(out.useAudibleSize)) {
                return false;
            }
        }
        else if (key == "getsFFTHolds") {
            if (!parseBool(out.getsFFTHolds)) {
                return false;
            }
        }
        else if (key == "isPerceptual") {
            if (!parseBool(out.isPerceptual)) {
                return false;
            }
        }
        else if (key == "isHannWindowed") {
            if (!parseBool(out.isHannWindowed)) {
                return false;
            }
        }
        else if (key == "isFFTdB"){
            if (!parseBool(out.isFFTdB)) {
                return false;
            }
        }
        else if (key == "usePeakRMSSmoothing") {
            if (!parseBool(out.usePeakRMSSmoothing)) {
                return false;
            }
        }
        else if (key == "isPeakRMSdB") {
            if (!parseBool(out.isPeakRMSdB)) {
                return false;
            }
        }
        else if (key == "getsPeakRMSHolds") {
            if (!parseBool(out.getsPeakRMSHolds)) {
                return false;
            }
        }
        else if (key == "isPeakRMSMono") {
            if (!parseBool(out.isPeakRMSMono)) {
                return false;
            }
        }
        else if (key == "feedbackBufferSize") {
            out.feedbackBufferSize = std::stoul(val);
        }
        else if (key == "feedbackBufferInitValue") {
            out.feedbackBufferInitValue = std::stof(val);
        }
        else {
            std::cerr << "parseSpec: line " << lineNum
                      << ": unknown key \"" << key << "\"\n";
            return false;
        }
    }
    return true;
}

