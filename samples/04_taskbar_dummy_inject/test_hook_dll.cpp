#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdio>

#pragma comment(lib, "shell32.lib")

namespace {

std::wstring logPath() {
    wchar_t appData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    std::filesystem::path p(appData);
    p /= L"AuraShell";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    p /= L"inject_test.log";
    return p.wstring();
}

void appendLog(const char* event) {
    auto path = logPath();
    std::ofstream f(path, std::ios::app);
    if (!f) return;

    // Timestamp: milliseconds since epoch (UTC)
    auto now    = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[%lld] test_hook_dll %s in PID %lu\n",
        static_cast<long long>(millis), event,
        static_cast<unsigned long>(GetCurrentProcessId()));
    f << buf;
}

} // anonymous namespace

BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(nullptr);
        appendLog("ATTACHED");
        break;

    case DLL_PROCESS_DETACH:
        appendLog("DETACHED");
        break;
    }
    return TRUE;
}
