#include "service_core.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>

#include "message_types.h"
#include "logging/logger.h"
#include "audio_engine.h"

#pragma comment(lib, "advapi32.lib")

namespace aura::service {

// ============================================================================
// Singleton
// ============================================================================

ServiceCore& ServiceCore::getInstance() {
    static ServiceCore instance;
    return instance;
}

ServiceCore::ServiceCore() {
    m_svcStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    m_svcStatus.dwCurrentState            = SERVICE_STOPPED;
    m_svcStatus.dwControlsAccepted        = SERVICE_ACCEPT_STOP |
                                            SERVICE_ACCEPT_SHUTDOWN;
    m_svcStatus.dwWin32ExitCode           = NO_ERROR;
    m_svcStatus.dwServiceSpecificExitCode = 0;
    m_svcStatus.dwCheckPoint              = 0;
    m_svcStatus.dwWaitHint                = 0;
}

ServiceCore::~ServiceCore() {
    if (m_running.load(std::memory_order_relaxed)) {
        shutdown();
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ServiceCore::initialize() {
    if (m_running.load(std::memory_order_relaxed)) {
        return true;
    }

    aura::logging::Logger::getInstance().info("service", "ServiceCore::initialize()");

    // Create the stop event (manual-reset, initially unsignalled).
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        aura::logging::Logger::getInstance().error(
            "service", "CreateEventW failed for stop event"
        );
        return false;
    }

    // Build the restricted DACL for the named pipe.
    if (!buildSecurePipeAttributes(m_pipeSa, m_pipeSdBuf)) {
        aura::logging::Logger::getInstance().warn(
            "service",
            "DACL construction failed — falling back to default security"
        );
        // Proceed without DACL; initialize() with nullptr = default security.
        if (!m_pipeServer.initialize()) {
            CloseHandle(m_stopEvent);
            m_stopEvent = nullptr;
            return false;
        }
    } else {
        if (!m_pipeServer.initialize(&m_pipeSa)) {
            aura::logging::Logger::getInstance().error(
                "service", "NamedPipeServer::initialize failed"
            );
            CloseHandle(m_stopEvent);
            m_stopEvent = nullptr;
            return false;
        }
    }

    m_startTickMs = GetTickCount64();
    m_running.store(true, std::memory_order_release);

    // Launch the IPC dispatch thread.
    m_ipcThread = std::thread([this] { ipcThreadProc(); });

    aura::logging::Logger::getInstance().info("service", "ServiceCore started");
    return true;
}

void ServiceCore::shutdown() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;  // already stopped
    }

    aura::logging::Logger::getInstance().info("service", "ServiceCore::shutdown()");

    // Signal the IPC thread to exit its wait.
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    m_pipeServer.shutdown();

    if (m_ipcThread.joinable()) {
        m_ipcThread.join();
    }

    aura::logging::Logger::getInstance().info("service", "ServiceCore stopped");
}

bool ServiceCore::isRunning() const noexcept {
    return m_running.load(std::memory_order_relaxed);
}

// ============================================================================
// Manager → Worker push
// ============================================================================

bool ServiceCore::pushTheme(std::wstring const& themeName) {
    if (!m_running.load(std::memory_order_relaxed)) return false;

    aura::ipc::Message msg;
    msg.messageType   = static_cast<uint32_t>(aura::ipc::MessageType::PUSH_THEME);
    msg.sequenceNumber = 0;

    aura::ipc::ThemePayload payload = {};
    size_t const copyLen = (std::min)(themeName.size(),
                                      size_t{255});  // leave room for null
    std::wmemcpy(payload.themeName, themeName.data(), copyLen);
    payload.animationSpeedPct = 100;
    msg.setPayload(payload);

    bool const ok = m_pipeServer.sendMessage(msg, 2000);
    if (ok) {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_currentTheme = themeName;
        // Convert wstring to narrow for the logger (UTF-8 safe).
        int const needed = WideCharToMultiByte(
            CP_UTF8, 0, themeName.c_str(), -1, nullptr, 0, nullptr, nullptr
        );
        std::string narrow(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
        if (needed > 0) {
            WideCharToMultiByte(
                CP_UTF8, 0, themeName.c_str(), -1,
                &narrow[0], needed, nullptr, nullptr
            );
        }
        aura::logging::Logger::getInstance().info("service", "Pushed theme: " + narrow);
    }
    return ok;
}

bool ServiceCore::pushConfig(std::string const& configJson) {
    if (!m_running.load(std::memory_order_relaxed)) return false;
    if (configJson.size() > 2048) return false;

    aura::ipc::Message msg;
    msg.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::PUSH_CONFIG);
    msg.sequenceNumber = 0;
    msg.payloadSize    = static_cast<uint32_t>(configJson.size());
    std::memcpy(msg.payload, configJson.data(), configJson.size());

    return m_pipeServer.sendMessage(msg, 2000);
}

bool ServiceCore::pushAudioBands() {
    if (!m_running.load(std::memory_order_relaxed)) return false;

    auto& engine = aura::audio::AudioEngine::getInstance();
    if (!engine.isInitialized()) return false;

    aura::ipc::AudioBandsPayload payload = {};
    auto bands = engine.getFrequencyBands();
    static_assert(bands.size() == 128, "Band count mismatch");
    std::copy(bands.begin(), bands.end(), payload.bands);
    payload.peak         = engine.getPeakLevel();
    payload.audioPresent = engine.isAudioPresent();

    aura::ipc::Message msg;
    msg.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::AUDIO_BANDS);
    msg.sequenceNumber = 0;
    msg.setPayload(payload);

    return m_pipeServer.sendMessage(msg, /*timeoutMs=*/50);
}

// ============================================================================
// Phase 8: Watchdog & event callback registration
// ============================================================================

void ServiceCore::setWorkerDisconnectCallback(WorkerDisconnectCallback cb) {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    m_workerDisconnectCallback = std::move(cb);
}

void ServiceCore::setThemeReceivedCallback(ThemeReceivedCallback cb) {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    m_themeReceivedCallback = std::move(cb);
}

// ============================================================================
// State queries
// ============================================================================

std::wstring ServiceCore::getCurrentTheme() const {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    return m_currentTheme;
}

uint32_t ServiceCore::uptimeSeconds() const noexcept {
    if (!m_startTickMs) return 0;
    return static_cast<uint32_t>((GetTickCount64() - m_startTickMs) / 1000);
}

// ============================================================================
// IPC dispatch thread — "lean loop": accept → dispatch → disconnect → repeat
// ============================================================================

void ServiceCore::ipcThreadProc() {
    aura::logging::Logger::getInstance().debug("service", "IPC thread started");

    constexpr uint32_t kAcceptTimeoutMs = 500;  // short poll so stop event is noticed

    while (m_running.load(std::memory_order_relaxed)) {
        // Check stop event without blocking.
        if (WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        if (!m_pipeServer.waitForClient(kAcceptTimeoutMs)) {
            continue;  // timeout or error — loop back and check stop event
        }

        uint32_t const clientPid = m_pipeServer.getConnectedClientPID();
        aura::logging::Logger::getInstance().debug(
            "service",
            "App connected (PID=" + std::to_string(clientPid) + ")"
        );

        bool clientDroppedUnexpectedly = true;

        // Per-client message loop.
        // kReceiveTimeoutMs must exceed the App's poll interval (5 s) so the
        // connection is not torn down between polls; 30 s leaves comfortable margin.
        constexpr uint32_t kReceiveTimeoutMs = 30000;
        while (m_running.load(std::memory_order_relaxed)) {
            aura::ipc::Message request;
            if (!m_pipeServer.receiveMessage(request, kReceiveTimeoutMs)) {
                break;  // client disconnected or timeout
            }
            if (!request.isValid()) continue;

            auto const type = static_cast<aura::ipc::MessageType>(request.messageType);

            aura::ipc::Message response;
            response.sequenceNumber = request.sequenceNumber;
            response.reserved       = 0;

            if (type == aura::ipc::MessageType::HANDSHAKE_REQUEST) {
                response.messageType = static_cast<uint32_t>(
                    aura::ipc::MessageType::HANDSHAKE_RESPONSE
                );
                aura::ipc::HandshakePayload rp = {};
                rp.clientPID      = GetCurrentProcessId();
                rp.clientVersion  = 0x0400;  // 4.0
                rp.capabilities   = 0x00FF;
                response.setPayload(rp);
                m_pipeServer.sendMessage(response, 2000);

            } else if (type == aura::ipc::MessageType::QUERY_STATE) {
                response.messageType = static_cast<uint32_t>(
                    aura::ipc::MessageType::STATUS_REPORT
                );
                aura::ipc::QueryStateResponse qr = {};
                {
                    std::lock_guard<std::mutex> lk(m_stateMutex);
                    size_t const n = (std::min)(m_currentTheme.size(), size_t{255});
                    std::wmemcpy(qr.currentTheme, m_currentTheme.data(), n);
                }
                qr.serviceVersion = 0x0400;
                qr.uptimeSeconds  = uptimeSeconds();
                qr.isElevated     = isCurrentProcessElevated();
                response.setPayload(qr);
                m_pipeServer.sendMessage(response, 2000);

            } else if (type == aura::ipc::MessageType::PUSH_THEME) {
                // Config App is pushing a theme change to the service (Worker → Manager).
                // Store it so QUERY_STATE reflects the new theme immediately.
                aura::ipc::ThemePayload const* tp = request.getPayload<aura::ipc::ThemePayload>();
                if (!tp) break;
                std::wstring const newTheme(tp->themeName);

                ThemeReceivedCallback themeCb;
                {
                    std::lock_guard<std::mutex> lk(m_stateMutex);
                    m_currentTheme = newTheme;
                    themeCb = m_themeReceivedCallback;
                }

                int const needed = WideCharToMultiByte(
                    CP_UTF8, 0, newTheme.c_str(), -1, nullptr, 0, nullptr, nullptr
                );
                std::string narrow(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
                if (needed > 0) {
                    WideCharToMultiByte(
                        CP_UTF8, 0, newTheme.c_str(), -1,
                        &narrow[0], needed, nullptr, nullptr
                    );
                }
                aura::logging::Logger::getInstance().info(
                    "service", "Theme received from client: " + narrow
                );

                if (themeCb) {
                    themeCb(newTheme);
                }

                response.messageType = static_cast<uint32_t>(aura::ipc::MessageType::ACK);
                response.payloadSize = 0;
                m_pipeServer.sendMessage(response, 2000);

            } else if (type == aura::ipc::MessageType::ACK) {
                // Graceful disconnect signal from client — not an unexpected drop.
                clientDroppedUnexpectedly = false;
                break;

            } else {
                // Unknown / unhandled: respond with ACK so the client doesn't hang.
                response.messageType = static_cast<uint32_t>(aura::ipc::MessageType::ACK);
                response.payloadSize = 0;
                m_pipeServer.sendMessage(response, 2000);
            }
        }

        m_pipeServer.disconnectClient();

        // Phase 8: Watchdog — if the client vanished without a graceful ACK and
        // the service itself is still running, fire the disconnect callback so the
        // host can attempt to restart the worker process.
        if (clientDroppedUnexpectedly && m_running.load(std::memory_order_relaxed)) {
            aura::logging::Logger::getInstance().warn(
                "service",
                "App disconnected unexpectedly (PID=" + std::to_string(clientPid) +
                ") — watchdog callback triggered"
            );
            WorkerDisconnectCallback watchdogCb;
            {
                std::lock_guard<std::mutex> lk(m_stateMutex);
                watchdogCb = m_workerDisconnectCallback;
            }
            if (watchdogCb) {
                watchdogCb(clientPid);
            }
        } else {
            aura::logging::Logger::getInstance().debug("service", "App disconnected");
        }
    }

    aura::logging::Logger::getInstance().debug("service", "IPC thread stopped");
}

// ============================================================================
// SCM status reporting
// ============================================================================

void ServiceCore::reportServiceStatus(
    DWORD const currentState,
    DWORD const exitCode,
    DWORD const waitHint
) noexcept {
    if (!m_statusHandle) return;

    m_svcStatus.dwCurrentState  = currentState;
    m_svcStatus.dwWin32ExitCode = exitCode;
    m_svcStatus.dwWaitHint      = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        m_svcStatus.dwControlsAccepted = 0;
    } else {
        m_svcStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    SetServiceStatus(m_statusHandle, &m_svcStatus);
}

// ============================================================================
// Windows Service entry points
// ============================================================================

void WINAPI ServiceCore::serviceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    ServiceCore& svc = getInstance();

    svc.m_statusHandle = RegisterServiceCtrlHandlerExW(
        SERVICE_NAME,
        serviceHandlerEx,
        &svc
    );
    if (!svc.m_statusHandle) {
        return;
    }

    svc.reportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    if (!svc.initialize()) {
        svc.reportServiceStatus(SERVICE_STOPPED, ERROR_FUNCTION_FAILED);
        return;
    }

    svc.reportServiceStatus(SERVICE_RUNNING);

    // Block until the stop event fires.
    WaitForSingleObject(svc.m_stopEvent, INFINITE);

    svc.reportServiceStatus(SERVICE_STOP_PENDING);
    svc.shutdown();
    svc.reportServiceStatus(SERVICE_STOPPED);
}

DWORD WINAPI ServiceCore::serviceHandlerEx(
    DWORD   const control,
    DWORD   /*eventType*/,
    LPVOID  /*eventData*/,
    LPVOID  const context
) {
    auto* const pSvc = static_cast<ServiceCore*>(context);
    if (!pSvc) return ERROR_CALL_NOT_IMPLEMENTED;

    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            pSvc->reportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
            if (pSvc->m_stopEvent) {
                SetEvent(pSvc->m_stopEvent);
            }
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            // SCM is asking for current status — just report it again.
            pSvc->reportServiceStatus(pSvc->m_svcStatus.dwCurrentState);
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

}  // namespace aura::service
