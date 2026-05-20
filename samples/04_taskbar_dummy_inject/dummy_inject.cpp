#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <filesystem>
#include <string>

// ============================================================================
// SAFETY CONTRACT (per CLAUDE.md)
// This sample ONLY injects into notepad.exe (a known-safe dummy target).
// It NEVER injects into explorer.exe, which would crash the Windows shell.
// Run with AV temporarily disabled if Windows Defender flags the injection —
// the DLL contains no hooks, no network calls, and no shell modifications.
// ============================================================================

namespace {

constexpr wchar_t TARGET_PROCESS[] = L"notepad.exe";

DWORD findProcessId(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{ .dwSize = sizeof(pe) };
    DWORD pid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool injectDll(DWORD pid, const std::wstring& dllPath) {
    // Open target process with rights needed for injection.
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProc) {
        wprintf(L"OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
        return false;
    }

    // Allocate memory in the target for the DLL path string.
    size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void*  pRemote   = VirtualAllocEx(hProc, nullptr, pathBytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemote) {
        wprintf(L"VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(hProc);
        return false;
    }

    // Write the DLL path into target memory.
    if (!WriteProcessMemory(hProc, pRemote, dllPath.c_str(), pathBytes, nullptr)) {
        wprintf(L"WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // Start a remote thread that calls LoadLibraryW with the DLL path.
    auto* pfnLoadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pfnLoadLib,
                                        pRemote, 0, nullptr);
    if (!hThread) {
        wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // Wait for injection to complete (LoadLibraryW returns).
    WaitForSingleObject(hThread, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    wprintf(L"LoadLibraryW returned handle: 0x%08lX\n", exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
    CloseHandle(hProc);

    return exitCode != 0; // LoadLibraryW returns nullptr on failure
}

} // anonymous namespace

int wmain(int argc, wchar_t* argv[]) {
    wprintf(L"AuraShell DLL injection sample — DUMMY TARGET ONLY\n");
    wprintf(L"Target: %ls\n\n", TARGET_PROCESS);

    // Locate the DLL next to this executable.
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(selfPath).parent_path();

    // Allow overriding the DLL path via command line.
    std::wstring dllPath;
    if (argc >= 2) {
        dllPath = argv[1];
    } else {
        dllPath = (exeDir / L"test_hook_dll.dll").wstring();
    }

    if (!std::filesystem::exists(dllPath)) {
        wprintf(L"ERROR: DLL not found at: %ls\n", dllPath.c_str());
        wprintf(L"Build test_hook_dll first, then re-run.\n");
        return 1;
    }
    wprintf(L"DLL path: %ls\n", dllPath.c_str());

    // Find notepad.exe.
    DWORD pid = findProcessId(TARGET_PROCESS);
    if (pid == 0) {
        wprintf(L"ERROR: %ls is not running. Please open Notepad first.\n",
                TARGET_PROCESS);
        return 1;
    }
    wprintf(L"Found %ls with PID %lu\n", TARGET_PROCESS, pid);

    // Inject.
    wprintf(L"Injecting...\n");
    bool ok = injectDll(pid, dllPath);

    if (ok) {
        wprintf(L"\nSUCCESS — DLL injected into %ls (PID %lu)\n", TARGET_PROCESS, pid);
        wprintf(L"Check %%LOCALAPPDATA%%\\AuraShell\\inject_test.log for the attach entry.\n");
    } else {
        wprintf(L"\nFAILED — see error messages above.\n");
        wprintf(L"Tip: Run this executable as Administrator.\n");
    }

    return ok ? 0 : 1;
}
