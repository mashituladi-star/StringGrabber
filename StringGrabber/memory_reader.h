#pragma once
#include <windows.h>
#include <string>
#include <vector>

inline std::string ReadAnsiString(HANDLE hProc, uintptr_t addr, size_t maxLen = 1024) {
    std::string out;
    char ch = 0; SIZE_T br = 0;
    for (size_t i = 0; i < maxLen; ++i) {
        if (!ReadProcessMemory(hProc, (LPCVOID)(addr + i), &ch, 1, &br) || br != 1) break;
        if (ch == '\0') break;
        out.push_back(ch);
    }
    return out;
}

inline std::wstring ReadWideString(HANDLE hProc, uintptr_t addr, size_t maxLen = 1024) {
    std::wstring out;
    wchar_t wc = 0; SIZE_T br = 0;
    for (size_t i = 0; i < maxLen; ++i) {
        if (!ReadProcessMemory(hProc, (LPCVOID)(addr + i * sizeof(wchar_t)),
            &wc, sizeof(wchar_t), &br) || br != sizeof(wchar_t)) break;
        if (wc == L'\0') break;
        out.push_back(wc);
    }
    return out;
}

inline bool LooksLikeAnsiString(HANDLE hProc, uintptr_t addr, size_t probe = 16) {
    char buf[16]; SIZE_T br = 0;
    if (!ReadProcessMemory(hProc, (LPCVOID)addr, buf, probe, &br) || br < 2) return false;
    int printable = 0;
    for (size_t i = 0; i < br && buf[i]; ++i) {
        if ((unsigned char)buf[i] >= 0x20) ++printable;
        else return false;
    }
    return printable >= 2;
}