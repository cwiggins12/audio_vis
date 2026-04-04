#pragma once

#include <fstream>
#include <iostream>
#include <chrono>

struct Log {
public:
    Log(const std::string& path) {
        origCout = std::cout.rdbuf();
        origCerr = std::cerr.rdbuf();
        logFile.open(path);
        if (logFile.is_open()) {
            std::cout.rdbuf(logFile.rdbuf());
            std::cerr.rdbuf(logFile.rdbuf());
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
        }
        auto now = std::chrono::system_clock::now();
        std::time_t ts = std::chrono::system_clock::to_time_t(now);
        std::cout << "=== audio_vis started: " << std::ctime(&ts);
    }

    ~Log() {
        std::cout.rdbuf(origCout);
        std::cerr.rdbuf(origCerr);
        logFile.close();
    }

private:
    std::streambuf* origCout;
    std::streambuf* origCerr;
    std::ofstream   logFile;
};

