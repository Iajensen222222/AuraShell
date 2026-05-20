#pragma once

#include <Windows.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <functional>

#include "named_pipe_server.h"
#include "pipe_security.h"

namespace aura::service {

// ============================================================================
// ServiceCore — Lean Windows Service skeleton (Phase 4)
//
// Responsibilities:
//   • Hosts the named pipe IPC server (Manager role)
//   • Dispatches HANDSHAKE_REQUEST, QUERY_STATE from the App (Worker)
//   • Pushes PUSH_THEME / PUSH_CONFIG to the connected App
//   • Implements SERVICE_CONTROL_STOP and SERVICE_CONTROL_SHUTDOWN
//
// Memory footprint target: < 2 MB (zero UI components, no DirectX).
//
// Thread model:
//   • Main thread   — ServiceMain / SCM interaction
//   • IPC thread    — blocked on named pipe accept/receive loop
//
// The IPC pipe is created with a DACL allowing SYSTEM + Interactive Users
// only, rejecting all other principals at the OS level.
// ============================================================================

class ServiceCore {
public:
    static ServiceCore& getInstance();

    // ========================================================================
    // Lifecycle (called by ServiceMain / unit tests)
    // ========================================================================

    bool initialize();
    void shutdown();

    [[nodiscard]] bool isRunning() const noexcept;

    // ========================================================================
    // Manager → Worker push operations
    // ========================================================================

    // Push a new theme name to the connected App.  Non-blocking if no client
    // is connected (returns false).
    bool pushTheme(std::wstring const& themeName);

    // Push a raw JSON config blob to the connected App.
    bool pushConfig(std::string const& configJson);

    // Push the current AudioEngine frequency bands to the connected App.
    // Call periodically (e.g., every 100ms) from the IPC dispatch thread.
    // Returns false when no client is connected or AudioEngine is not initialized.
    bool pushAudioBands();

    // Push live CPU/memory/FPS stats to the connected App (~0.5fps).
    // Returns false when no client or PerformanceLogger is not initialized.
    bool pushPerformanceStats();

    // ========================================================================
    // State queries (thread-safe)
    // ========================================================================

    [[nodiscard]] std::wstring getCurrentTheme() const;
    [[nodiscard]] uint32_t     uptimeSeconds()   const noexcept;

    // ========================================================================
    // Phase 8: Watchdog & event callbacks
    //
    // setWorkerDisconnectCallback — fires on the IPC thread whenever the
    //   connected client drops without a graceful SERVICE_CONTROL_STOP.
    //   The callback receives the client PID that disconnected.
    //   Intended use: host process restarts AuraShell.exe via CreateProcessW.
    //
    // setThemeReceivedCallback — fires when the Config App sends PUSH_THEME
    //   to the service (Worker → Manager direction).  The service stores the
    //   new theme name in m_currentTheme before invoking this callback, so
    //   getCurrentTheme() is already updated when the callback runs.
    // ========================================================================

    using WorkerDisconnectCallback = std::function<void(uint32_t clientPid)>;
    using ThemeReceivedCallback    = std::function<void(std::wstring const& themeName)>;

    void setWorkerDisconnectCallback(WorkerDisconnectCallback cb);
    void setThemeReceivedCallback(ThemeReceivedCallback cb);

    // ========================================================================
    // Windows Service entry points — registered with SCM via
    // StartServiceCtrlDispatcher.  In unit tests these are NOT called;
    // initialize() / shutdown() are used directly instead.
    // ========================================================================

    static void  WINAPI serviceMain(DWORD argc, LPWSTR* argv);
    static DWORD WINAPI serviceHandlerEx(
        DWORD control, DWORD eventType,
        LPVOID eventData, LPVOID context
    );

private:
    ServiceCore();
    ~ServiceCore();

    ServiceCore(ServiceCore const&)            = delete;
    ServiceCore& operator=(ServiceCore const&) = delete;

    void ipcThreadProc();
    void reportServiceStatus(DWORD currentState,
                             DWORD exitCode  = NO_ERROR,
                             DWORD waitHint  = 0) noexcept;

    // ---- IPC ----------------------------------------------------------------
    aura::ipc::NamedPipeServer m_pipeServer;
    SECURITY_ATTRIBUTES        m_pipeSa    = {};
    std::vector<uint8_t>       m_pipeSdBuf;

    // ---- Threading ----------------------------------------------------------
    std::thread       m_ipcThread;
    std::atomic<bool> m_running{false};
    HANDLE            m_stopEvent{nullptr};  // manual-reset event; set on shutdown

    // ---- SCM ----------------------------------------------------------------
    SERVICE_STATUS_HANDLE m_statusHandle{nullptr};
    SERVICE_STATUS        m_svcStatus{};

    // ---- State (guarded by m_stateMutex) ------------------------------------
    mutable std::mutex m_stateMutex;
    std::wstring       m_currentTheme{L"default"};
    ULONGLONG          m_startTickMs{0};  // GetTickCount64 at initialize()

    // ---- Phase 8: Watchdog callbacks (set before initialize(), read-only after) --
    WorkerDisconnectCallback m_workerDisconnectCallback;
    ThemeReceivedCallback    m_themeReceivedCallback;

    static constexpr wchar_t const* SERVICE_NAME = L"AuraShellService";
};

}  // namespace aura::service
