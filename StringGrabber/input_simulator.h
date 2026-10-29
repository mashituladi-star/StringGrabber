#pragma once
#include <windows.h>

inline bool SimulateCtrlV() {
    INPUT in[4] = {};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LCONTROL;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V'; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_LCONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT n = SendInput(4, in, sizeof(INPUT));
    Sleep(40);
    return n == 4;
}

inline bool SimulateEnter() {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_RETURN;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_RETURN; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT n = SendInput(2, in, sizeof(INPUT));
    Sleep(30);
    return n == 2;
}