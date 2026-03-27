#pragma once

#include <string>
#include <stdexcept>
#include <cctype>
#include <iostream>
#include <cstdint>
#include <bitset>

static constexpr int EXPR_VAR_AMT = 6;

enum ExprVariable {
    WINDOW_WIDTH,
    WINDOW_HEIGHT,
    DISPLAY_HZ,
    NUM_CHANNELS,
    SAMPLE_RATE,
    FFT_SIZE,
};

struct ExprContext {
    uint32_t windowWidth    = 0;
    uint32_t windowHeight   = 0;
    uint32_t numChannels    = 0;
    uint32_t displayHz      = 0;
    uint32_t sampleRate     = 0;
    uint32_t fftSize        = 0;
    //added usage bools to determine usage to set in spec
    //basically free, since these are only ever stack allocated
    //wish just checking vars for non zero could work,
    //but these can get parsed before the vars are known :(
    std::bitset<EXPR_VAR_AMT> uses{};
};

struct ExprParser {
    const std::string& src;
    ExprContext& ctx;
    size_t pos = 0;

    ExprParser(const std::string& s, ExprContext& c)
        : src(s), ctx(c), pos(0) {}

    void skipWhitespace() {
        while (pos < src.size() && std::isspace(src[pos])) pos++;
    }

    uint32_t parsePrimary() {
        skipWhitespace();
        if (pos >= src.size())
            throw std::runtime_error("unexpected end of expression");

        // parentheses
        if (src[pos] == '(') {
            pos++;
            uint32_t val = parseAddSub();
            skipWhitespace();
            if (pos >= src.size() || src[pos] != ')')
                throw std::runtime_error("missing closing parenthesis");
            pos++;
            return val;
        }

        // integer literal
        if (std::isdigit(src[pos])) {
            uint32_t val = 0;
            while (pos < src.size() && std::isdigit(src[pos]))
                val = val * 10 + (src[pos++] - '0');
            return val;
        }

        // named variable
        if (std::isalpha(src[pos]) || src[pos] == '_') {
            size_t start = pos;
            while (pos < src.size() && (std::isalnum(src[pos]) || src[pos] == '_'))
                pos++;
            std::string name = src.substr(start, pos - start);

            if (name == "WINDOW_WIDTH")  ctx.uses[WINDOW_WIDTH] = true;    return ctx.windowWidth;
            if (name == "WINDOW_HEIGHT") ctx.uses[WINDOW_HEIGHT] = true;   return ctx.windowHeight;
            if (name == "DISPLAY_HZ")    ctx.uses[DISPLAY_HZ] = true;       return ctx.displayHz;
            if (name == "NUM_CHANNELS")  ctx.uses[NUM_CHANNELS] = true; return ctx.numChannels;
            if (name == "SAMPLE_RATE")   ctx.uses[SAMPLE_RATE] = true;       return ctx.sampleRate;
            if (name == "FFT_SIZE")      ctx.uses[FFT_SIZE] = true;      return ctx.fftSize;

            throw std::runtime_error("unknown variable: " + name);
        }

        throw std::runtime_error(
            std::string("unexpected character: ") + src[pos]);
    }

    uint32_t parseMulDiv() {
        uint32_t left = parsePrimary();
        while (true) {
            skipWhitespace();
            if (pos >= src.size()) break;
            char op = src[pos];
            if (op != '*' && op != '/') break;
            pos++;
            uint32_t right = parsePrimary();
            if (op == '*') {
                left *= right;
            } else {
                if (right == 0)
                    throw std::runtime_error("division by zero");
                left /= right;
            }
        }
        return left;
    }

    uint32_t parseAddSub() {
        uint32_t left = parseMulDiv();
        while (true) {
            skipWhitespace();
            if (pos >= src.size()) break;
            char op = src[pos];
            if (op != '+' && op != '-') break;
            pos++;
            uint32_t right = parseMulDiv();
            if (op == '+') {
                left += right;
            } else {
                left = (right > left) ? 0 : left - right;
            }
        }
        return left;
    }

    uint32_t evaluate() {
        uint32_t result = parseAddSub();
        skipWhitespace();
        if (pos < src.size())
            throw std::runtime_error(
                std::string("unexpected character after expression: ") + src[pos]);
        return result;
    }
};

// returns false and logs on failure, writes result to out on success
inline bool evalExpr(const std::string& expr, ExprContext& ctx,
                     uint32_t& out, std::bitset<EXPR_VAR_AMT>& uses, int lineNum = -1) {
    try {
        ExprParser p(expr, ctx);
        uint32_t result = p.evaluate();
        out = result;
        uses = ctx.uses;
        return true;
    } catch (const std::exception& e) {
        if (lineNum >= 0)
            std::cerr << "parseSpec: line " << lineNum
                      << ": expression error: " << e.what()
                      << " in \"" << expr << "\"\n";
        else
            std::cerr << "evalExpr: " << e.what()
                      << " in \"" << expr << "\"\n";
        return false;
    }
}

