// AuraShellService.exe — SCM entry point + self-registration CLI.
//
// Usage (must be run as Administrator for install/uninstall/start/stop):
//   AuraShellService.exe             — normal SCM dispatch (launched by Windows)
//   AuraShellService.exe --console   — run IPC server in-process (Ctrl+C to stop)
//   AuraShellService.exe --install   — register service with SCM (AUTO_START)
//   AuraShellService.exe --uninstall — stop and delete service registration
//   AuraShellService.exe --start     — start the registered service
//   AuraShellService.exe --stop      — send STOP control to the running service
//
// The installer calls --install then --start.
// The uninstaller calls --stop then --uninstall.

#include <Windows.h>
#include <string>
#include <cstdio>

#include "service_core.h"

#pragma comment(lib, "advapi32.lib")

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

static constexpr wchar_t const* SVC_NAME    = L"AuraShellService";
static constexpr wchar_t const* SVC_DISPLAY = L"AuraShell Visual Service";
static constexpr wchar_t const* SVC_DESC    = L"Manages AuraShell icon overlays and IPC for the taskbar visual engine.";

void printResult(char const* action, bool const ok, DWORD const err = 0) {
    if (ok) {
        std::printf("[OK]  %s\n", action);
    } else {
        std::printf("[ERR] %s — Win32 error %lu\n", action, err ? err : GetLastError());
    }
}

// Open the SCM with the requested access right.
SC_HANDLE openSCM(DWORD const access) {
    SC_HANDLE h = OpenSCManagerW(nullptr, nullptr, access);
    if (!h) {
        printResult("OpenSCManager", false);
    }
    return h;
}

// ---- --install --------------------------------------------------------------

int doInstall() {
    std::printf("AuraShell Service Installer\n");
    std::printf("---------------------------\n");

    SC_HANDLE const hSCM = openSCM(SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) return 1;

    // Use the absolute path of the currently running executable so the SCM
    // can always locate the binary, regardless of working directory.
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        printResult("GetModuleFileNameW", false);
        CloseServiceHandle(hSCM);
        return 1;
    }

    // Quote the path to handle spaces in "Program Files".
    std::wstring const binPath = std::wstring(L"\"") + exePath + L"\"";

    SC_HANDLE const hSvc = CreateServiceW(
        hSCM,
        SVC_NAME,
        SVC_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,         // start with Windows
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr,   // load order group
        nullptr,   // tag id
        nullptr,   // dependencies
        nullptr,   // run as LocalSystem
        nullptr    // password
    );

    if (!hSvc) {
        DWORD const err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            std::printf("[INFO] Service already registered — run --uninstall first if you need to update.\n");
            CloseServiceHandle(hSCM);
            return 0;
        }
        printResult("CreateService", false, err);
        CloseServiceHandle(hSCM);
        return 1;
    }

    // Set the service description.
    SERVICE_DESCRIPTIONW desc = {};
    desc.lpDescription = const_cast<LPWSTR>(SVC_DESC);
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Configure recovery: restart the service after the first two failures;
    // reset the failure count after 1 day of uptime.
    SC_ACTION const actions[3] = {
        {SC_ACTION_RESTART, 5000},   // restart after 5s on 1st failure
        {SC_ACTION_RESTART, 10000},  // restart after 10s on 2nd failure
        {SC_ACTION_NONE,    0}       // do nothing on subsequent failures
    };
    SERVICE_FAILURE_ACTIONSW failureActions = {};
    failureActions.dwResetPeriod  = 86400;   // 24 hours
    failureActions.lpRebootMsg    = nullptr;
    failureActions.lpCommand      = nullptr;
    failureActions.cActions       = 3;
    failureActions.lpsaActions    = const_cast<SC_ACTION*>(actions);
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions);

    printResult("CreateService (registered)", true);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return 0;
}

// ---- --uninstall ------------------------------------------------------------

int doUninstall() {
    std::printf("AuraShell Service Uninstaller\n");
    std::printf("-----------------------------\n");

    SC_HANDLE const hSCM = openSCM(SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return 1;

    SC_HANDLE const hSvc = OpenServiceW(hSCM, SVC_NAME,
                                        SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!hSvc) {
        DWORD const err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::printf("[INFO] Service is not registered — nothing to remove.\n");
            CloseServiceHandle(hSCM);
            return 0;
        }
        printResult("OpenService", false, err);
        CloseServiceHandle(hSCM);
        return 1;
    }

    // Stop the service if it is running.
    SERVICE_STATUS_PROCESS ssp = {};
    DWORD needed = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<BYTE*>(&ssp), sizeof(ssp), &needed)) {
        if (ssp.dwCurrentState != SERVICE_STOPPED) {
            SERVICE_STATUS ss = {};
            ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);

            // Poll until stopped or timed out (10 s).
            DWORD const deadline = GetTickCount() + 10000;
            while (GetTickCount() < deadline) {
                Sleep(250);
                if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                         reinterpret_cast<BYTE*>(&ssp), sizeof(ssp), &needed) &&
                    ssp.dwCurrentState == SERVICE_STOPPED) {
                    break;
                }
            }
            printResult("StopService", ssp.dwCurrentState == SERVICE_STOPPED);
        }
    }

    bool const deleted = (DeleteService(hSvc) != FALSE);
    printResult("DeleteService", deleted);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return deleted ? 0 : 1;
}

// ---- --start ----------------------------------------------------------------

int doStart() {
    std::printf("Starting AuraShellService...\n");

    SC_HANDLE const hSCM = openSCM(SC_MANAGER_CONNECT);
    if (!hSCM) return 1;

    SC_HANDLE const hSvc = OpenServiceW(hSCM, SVC_NAME,
                                        SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        printResult("OpenService", false);
        CloseServiceHandle(hSCM);
        return 1;
    }

    bool ok = (StartServiceW(hSvc, 0, nullptr) != FALSE);
    if (!ok && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
        std::printf("[INFO] Service is already running.\n");
        ok = true;
    }

    if (ok) {
        // Wait up to 10 s for SERVICE_RUNNING.
        SERVICE_STATUS_PROCESS ssp = {};
        DWORD needed = 0;
        DWORD const deadline = GetTickCount() + 10000;
        while (GetTickCount() < deadline) {
            Sleep(250);
            if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                     reinterpret_cast<BYTE*>(&ssp), sizeof(ssp), &needed) &&
                ssp.dwCurrentState == SERVICE_RUNNING) {
                break;
            }
        }
        ok = (ssp.dwCurrentState == SERVICE_RUNNING);
    }

    printResult("StartService", ok);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok ? 0 : 1;
}

// ---- --stop -----------------------------------------------------------------

int doStop() {
    std::printf("Stopping AuraShellService...\n");

    SC_HANDLE const hSCM = openSCM(SC_MANAGER_CONNECT);
    if (!hSCM) return 1;

    SC_HANDLE const hSvc = OpenServiceW(hSCM, SVC_NAME,
                                        SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD const err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::printf("[INFO] Service is not registered.\n");
            CloseServiceHandle(hSCM);
            return 0;
        }
        printResult("OpenService", false, err);
        CloseServiceHandle(hSCM);
        return 1;
    }

    SERVICE_STATUS ss = {};
    bool ok = (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss) != FALSE);
    if (!ok && GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
        std::printf("[INFO] Service is already stopped.\n");
        ok = true;
    }

    if (ok) {
        // Wait up to 10 s for SERVICE_STOPPED.
        SERVICE_STATUS_PROCESS ssp = {};
        DWORD needed = 0;
        DWORD const deadline = GetTickCount() + 10000;
        while (GetTickCount() < deadline) {
            Sleep(250);
            if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                     reinterpret_cast<BYTE*>(&ssp), sizeof(ssp), &needed) &&
                ssp.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
        }
        ok = (ssp.dwCurrentState == SERVICE_STOPPED);
    }

    printResult("StopService", ok);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok ? 0 : 1;
}

}  // namespace

// ============================================================================
// wmain — entry point
// ============================================================================

int wmain(int const argc, wchar_t const* const argv[]) {
    // Parse the first argument (if any).
    std::wstring const arg = (argc >= 2) ? argv[1] : L"";

    if (arg == L"--console") {
        // Run the IPC server in-process without SCM (useful for development /
        // integration testing).  Press Ctrl+C or close the window to stop.
        std::printf("AuraShellService [console mode] — press Ctrl+C to stop\n");
        std::printf("Named pipe: \\\\.\\pipe\\AuraShell_Control\n\n");

        aura::service::ServiceCore& svc = aura::service::ServiceCore::getInstance();
        if (!svc.initialize()) {
            std::printf("[ERR] ServiceCore::initialize() failed\n");
            return 1;
        }
        std::printf("[OK]  Service running — waiting for AuraConfig app connection\n");

        // Block until Ctrl+C (SetConsoleCtrlHandler would be cleaner, but
        // WaitForSingleObject on a manual event is fine for a dev-mode tool).
        HANDLE hStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        SetConsoleCtrlHandler([](DWORD) -> BOOL {
            // Signal the stop event on any console event (Ctrl+C, close, etc.)
            HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"AuraShellConsoleModeStop");
            if (h) { SetEvent(h); CloseHandle(h); }
            return TRUE;
        }, TRUE);
        // Give the ctrl handler a named event to signal.
        CloseHandle(hStop);
        hStop = CreateEventW(nullptr, TRUE, FALSE, L"AuraShellConsoleModeStop");
        WaitForSingleObject(hStop, INFINITE);
        CloseHandle(hStop);

        std::printf("\n[OK]  Shutting down...\n");
        svc.shutdown();
        return 0;
    }

    if (arg == L"--install") {
        return doInstall();
    }
    if (arg == L"--uninstall") {
        return doUninstall();
    }
    if (arg == L"--start") {
        return doStart();
    }
    if (arg == L"--stop") {
        return doStop();
    }

    // No recognized flag → normal SCM dispatch path.
    SERVICE_TABLE_ENTRYW const dispatchTable[] = {
        { const_cast<LPWSTR>(SVC_NAME),
          aura::service::ServiceCore::serviceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        DWORD const err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Launched from a terminal without a flag — print usage.
            std::printf(
                "AuraShellService.exe\n"
                "  (no args)    Run as Windows Service (SCM only)\n"
                "  --install    Register service with SCM (requires Admin)\n"
                "  --uninstall  Stop and remove service registration (requires Admin)\n"
                "  --start      Start the registered service (requires Admin)\n"
                "  --stop       Stop the running service (requires Admin)\n"
            );
            return 0;
        }
        return static_cast<int>(err);
    }
    return 0;
}
