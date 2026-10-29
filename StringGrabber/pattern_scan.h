#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cctype>
#include <cstdlib>

struct PatternByte {
    uint8_t value = 0;
    uint8_t mask = 0;   // 0 = wildcard, 0xFF = точное совпадение
};

inline std::vector<PatternByte> ParsePattern(const std::string& pattern) {
    std::vector<PatternByte> out;
    size_t i = 0;
    while (i < pattern.size()) {
        while (i < pattern.size() && std::isspace((unsigned char)pattern[i])) ++i;
        if (i >= pattern.size()) break;
        if (pattern[i] == '?') {
            out.push_back({ 0, 0 });
            ++i;
            if (i < pattern.size() && pattern[i] == '?') ++i;
        }
        else {
            char* end = nullptr;
            long v = std::strtol(pattern.c_str() + i, &end, 16);
            if (end == pattern.c_str() + i) break;
            out.push_back({ (uint8_t)v, 0xFF });
            i = (size_t)(end - pattern.c_str());
        }
    }
    return out;
}

inline uint8_t* FindPatternInBuffer(const uint8_t* base, size_t size, const std::string& pattern) {
    auto parsed = ParsePattern(pattern);
    if (parsed.empty() || size < parsed.size()) return nullptr;

    const size_t patLen = parsed.size();
    for (size_t i = 0; i <= size - patLen; ++i) {
        bool ok = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (parsed[j].mask != 0 && base[i + j] != parsed[j].value) { ok = false; break; }
        }
        if (ok) return const_cast<uint8_t*>(base + i);
    }
    return nullptr;
}

inline std::vector<uint8_t*> FindAllPatternInBuffer(
    const uint8_t* base, size_t size, const std::string& pattern)
{
    std::vector<uint8_t*> hits;
    auto parsed = ParsePattern(pattern);
    if (parsed.empty() || size < parsed.size()) return hits;

    const size_t patLen = parsed.size();
    for (size_t i = 0; i <= size - patLen; ++i) {
        bool ok = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (parsed[j].mask != 0 && base[i + j] != parsed[j].value) { ok = false; break; }
        }
        if (ok) hits.push_back(const_cast<uint8_t*>(base + i));
    }
    return hits;
}