#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

#include "pattern_scan.h"
#include "memory_reader.h"
#include "clipboard_utils.h"
#include "input_simulator.h"

static constexpr const char* kPattern = "4C 89 E6 48 8D 15 ?? ?? ?? ?? B9";

static bool IEquals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

static DWORD FindPidByName(const char* name) {
    wchar_t wname[MAX_PATH] = {};
    if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, MAX_PATH)) return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wname) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

struct StrRef { uintptr_t addr; uint32_t len; };

static bool ResolveStringRef(uintptr_t found, HANDLE hProc, StrRef& out) {
    uintptr_t leaAddr = found + 3;
    int32_t disp = 0;
    SIZE_T br = 0;
    if (!ReadProcessMemory(hProc, (LPCVOID)(leaAddr + 3), &disp, 4, &br) || br != 4)
        return false;

    out.addr = leaAddr + 7 + (intptr_t)disp;

    uint32_t len = 0;
    if (ReadProcessMemory(hProc, (LPCVOID)(found + 11), &len, 4, &br) && br == 4) {
        if (len > 0 && len < 0x10000) { out.len = len; return true; }
    }
    out.len = 0;
    return true;
}

static bool ReadStringByRef(HANDLE hProc, const StrRef& ref,
    std::string& a, std::wstring& w, bool& isAnsi)
{
    if (ref.len > 0) {
        std::vector<char> buf(ref.len);
        SIZE_T br = 0;
        if (!ReadProcessMemory(hProc, (LPCVOID)ref.addr, buf.data(), ref.len, &br) || br != ref.len)
            return false;

        bool wide = (ref.len >= 4 && (ref.len % 2) == 0);
        if (wide) {
            for (size_t i = 1; i < ref.len; i += 2)
                if (buf[i] != 0) { wide = false; break; }
        }
        if (wide) {
            isAnsi = false;
            w.assign((const wchar_t*)buf.data(), ref.len / 2);
        }
        else {
            isAnsi = true;
            a.assign(buf.data(), ref.len);
        }
        return !a.empty() || !w.empty();
    }

    isAnsi = LooksLikeAnsiString(hProc, ref.addr);
    if (isAnsi) { a = ReadAnsiString(hProc, ref.addr); return !a.empty(); }
    w = ReadWideString(hProc, ref.addr); return !w.empty();
}

static bool ScanModule(HANDLE hProc, HMODULE hMod, const char* modName,
    std::string& outA, std::wstring& outW, bool& outAnsi)
{
    MODULEINFO mi{};
    if (!GetModuleInformation(hProc, hMod, &mi, sizeof(mi))) return false;

    auto* base = (uint8_t*)mi.lpBaseOfDll;
    size_t size = mi.SizeOfImage;
    if (size == 0) return false;

    std::vector<uint8_t> img(size);
    SIZE_T read = 0;
    if (!ReadProcessMemory(hProc, base, img.data(), size, &read) || read == 0)
        return false;

    auto hits = FindAllPatternInBuffer(img.data(), read, kPattern);
    if (hits.empty()) return false;

    printf("  [%s] %zu hit(s)\n", modName, hits.size());

    for (auto* local : hits) {
        uintptr_t found = (uintptr_t)(base + (local - img.data()));
        StrRef ref{};
        if (!ResolveStringRef(found, hProc, ref)) continue;
        if (!ReadStringByRef(hProc, ref, outA, outW, outAnsi)) continue;

        printf("  [%s] found @ %p -> str @ %p len=%u ansi=%d\n",
            modName, (void*)found, (void*)ref.addr, ref.len, (int)outAnsi);
        return true;
    }
    return false;
}

struct EnumCtx { DWORD pid; HWND found; };

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    EnumCtx* ctx = (EnumCtx*)lp;
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowTextLengthA(hwnd) == 0) return TRUE;

    char cls[128] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strstr(cls, "IME") || strstr(cls, "MSCTFIME")) return TRUE;

    ctx->found = hwnd;
    return FALSE;
}

static HWND FindMainWindowByPid(DWORD pid) {
    EnumCtx ctx{ pid, nullptr };
    EnumWindows(EnumProc, (LPARAM)&ctx);
    return ctx.found;
}

static HWND FindConsoleWindowForPid(DWORD pid) {
    HWND our = GetConsoleWindow();
    FreeConsole();
    HWND target = nullptr;
    if (AttachConsole(pid)) {
        target = GetConsoleWindow();
        FreeConsole();
    }
    if (our) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);
    }
    return target;
}

static void ForceForeground(HWND hwnd) {
    if (!hwnd) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();

    bool attached = false;
    if (fgThread && fgThread != myThread)
        attached = AttachThreadInput(myThread, fgThread, TRUE) != FALSE;

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
    Sleep(200);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 2) {
        printf("Usage: %s <process_name|pid> [module_name]\n", argv[0]);
        printf("Example: %s my-first-crackme.exe\n", argv[0]);
        printf("         %s my-first-crackme.exe GameAssembly.dll\n", argv[0]);
        return 1;
    }

    DWORD pid = 0;
    if (isdigit((unsigned char)argv[1][0])) pid = (DWORD)strtoul(argv[1], nullptr, 10);
    else pid = FindPidByName(argv[1]);

    if (!pid) { printf("[!] Process '%s' not found\n", argv[1]); return 2; }
    printf("[+] PID = %lu\n", pid);

    HANDLE hProc = OpenProcess(
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
        FALSE, pid);
    if (!hProc) {
        printf("[!] OpenProcess failed (err=%lu). Run as admin if target is elevated.\n",
            GetLastError());
        return 3;
    }

    HMODULE mods[1024]; DWORD needed = 0;
    if (!EnumProcessModulesEx(hProc, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        printf("[!] EnumProcessModulesEx failed (err=%lu)\n", GetLastError());
        CloseHandle(hProc);
        return 4;
    }
    DWORD count = needed / sizeof(HMODULE);

    std::string targetModule = (argc >= 3) ? argv[2] : "";

    std::string foundA; std::wstring foundW; bool foundAnsi = true;
    bool ok = false;

    for (DWORD i = 0; i < count && !ok; ++i) {
        char nameA[MAX_PATH] = {};
        if (!GetModuleBaseNameA(hProc, mods[i], nameA, MAX_PATH)) continue;
        if (!targetModule.empty() && !IEquals(nameA, targetModule.c_str())) continue;
        ok = ScanModule(hProc, mods[i], nameA, foundA, foundW, foundAnsi);
    }

    CloseHandle(hProc);

    if (!ok) {
        printf("[!] Pattern not found in target process.\n");
        return 5;
    }

    if (foundAnsi) {
        printf("[+] String (ANSI, %zu bytes): \"%s\"\n", foundA.size(), foundA.c_str());
        if (!SetClipboardAnsiText(foundA)) { printf("[!] Clipboard set failed\n"); return 6; }
    }
    else {
        printf("[+] String (UTF-16, %zu wchars)\n", foundW.size());
        if (!SetClipboardWideText(foundW)) { printf("[!] Clipboard set failed\n"); return 6; }
    }

    HWND targetWnd = FindMainWindowByPid(pid);
    if (!targetWnd) targetWnd = FindConsoleWindowForPid(pid);

    if (targetWnd) {
        printf("[+] Target window HWND=%p, activating...\n", (void*)targetWnd);
        ForceForeground(targetWnd);
        Sleep(500);
    }
    else {
        printf("[!] Target window not found automatically.\n");
        printf("[*] Switch to the target input field NOW (5 sec)...\n");
        for (int i = 5; i > 0; --i) { printf("  %d...\n", i); Sleep(1000); }
    }

    if (!SimulateCtrlV()) {
        printf("[!] SendInput Ctrl+V failed (err=%lu). UIPI? Run as admin.\n", GetLastError());
        return 7;
    }
    if (!SimulateEnter()) {
        printf("[!] SendInput Enter failed (err=%lu).\n", GetLastError());
        return 8;
    }

    printf("[+] Done.\n");
    return 0;
}