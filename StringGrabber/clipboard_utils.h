#pragma once
#include <windows.h>
#include <string>
#include <cstring>

inline bool SetClipboardAnsiText(const std::string& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    HGLOBAL hA = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hA) {
        void* p = GlobalLock(hA);
        if (p) { memcpy(p, text.c_str(), text.size() + 1); GlobalUnlock(hA); }
        if (!SetClipboardData(CF_TEXT, hA)) GlobalFree(hA);
    }
    int wlen = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        HGLOBAL hW = GlobalAlloc(GMEM_MOVEABLE, (size_t)wlen * sizeof(wchar_t));
        if (hW) {
            wchar_t* pw = (wchar_t*)GlobalLock(hW);
            if (pw) { MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, pw, wlen); GlobalUnlock(hW); }
            if (!SetClipboardData(CF_UNICODETEXT, hW)) GlobalFree(hW);
        }
    }
    CloseClipboard();
    return true;
}

inline bool SetClipboardWideText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) { CloseClipboard(); return false; }
    void* p = GlobalLock(h);
    if (!p) { GlobalFree(h); CloseClipboard(); return false; }
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(h);
    if (!SetClipboardData(CF_UNICODETEXT, h)) { GlobalFree(h); CloseClipboard(); return false; }
    CloseClipboard();
    return true;
}