#include "app_client.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>

#include "logging/logger.h"

namespace aura::app {

AppClient::AppClient()  = default;
AppClient::~AppClient() { disconnect(); }

// ============================================================================
// connect
// ============================================================================

AppClient::ConnectResult AppClient::connect(uint32_t const timeoutMs) {
    // WaitNamedPipeW gives us a clean error code before we attempt a real
    // connection.  If the pipe doesn't exist the service isn't running.
    if (!WaitNamedPipeW(aura::ipc::NamedPipeClient::PIPE_NAME, timeoutMs)) {
        DWORD const err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) return ConnectResult::AccessDenied;
        return ConnectResult::ServiceNotRunning;  // NOT_FOUND or timeout
    }

    if (!m_pipe.connect(timeoutMs, /*maxRetries=*/0)) {
        return ConnectResult::Unknown;
    }

    // Send HANDSHAKE_REQUEST and wait for HANDSHAKE_RESPONSE.
    aura::ipc::Message req;
    req.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::HANDSHAKE_REQUEST);
    req.sequenceNumber = m_nextSeq++;

    aura::ipc::HandshakePayload hp = {};
    hp.clientPID     = GetCurrentProcessId();
    hp.clientVersion = 0x0500;  // App protocol version 5.0
    hp.capabilities  = 0x00FF;
    req.setPayload(hp);

    aura::ipc::Message resp;
    if (!m_pipe.sendMessage(req, timeoutMs) ||
        !m_pipe.receiveMessage(resp, timeoutMs)) {
        m_pipe.disconnect();
        return ConnectResult::Unknown;
    }

    aura::logging::Logger::getInstance().info(
        "app_client", "Connected to AuraShellService"
    );
    return ConnectResult::Connected;
}

void AppClient::disconnect() {
    if (m_pipe.isConnected()) {
        m_pipe.disconnect();
        aura::logging::Logger::getInstance().info(
            "app_client", "Disconnected from AuraShellService"
        );
    }
}

bool AppClient::isConnected() const noexcept {
    return m_pipe.isConnected();
}

// ============================================================================
// queryState
// ============================================================================

bool AppClient::queryState(aura::ipc::QueryStateResponse& out) {
    if (!m_pipe.isConnected()) return false;

    aura::ipc::Message req;
    req.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::QUERY_STATE);
    req.sequenceNumber = m_nextSeq++;
    req.payloadSize    = 0;

    if (!m_pipe.sendMessage(req, 2000)) return false;

    aura::ipc::Message resp;
    if (!m_pipe.receiveMessage(resp, 2000)) return false;

    aura::ipc::QueryStateResponse const* const p =
        resp.getPayload<aura::ipc::QueryStateResponse>();
    if (!p) return false;

    out = *p;
    return true;
}

// ============================================================================
// pushTheme
// ============================================================================

bool AppClient::pushTheme(ThemeConfig const& theme) {
    if (!m_pipe.isConnected()) return false;

    aura::ipc::Message req;
    req.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::PUSH_THEME);
    req.sequenceNumber = m_nextSeq++;

    aura::ipc::ThemePayload tp = {};
    size_t const n = (std::min)(theme.themeName.size(), size_t{255});
    std::wmemcpy(tp.themeName, theme.themeName.data(), n);
    tp.animationSpeedPct = theme.animSpeedPct;
    tp.colorCount        = 0;
    req.setPayload(tp);

    if (!m_pipe.sendMessage(req, 2000)) return false;

    // Consume the ACK so the server's receive loop stays in sync.
    aura::ipc::Message ack;
    m_pipe.receiveMessage(ack, 2000);
    return true;
}

}  // namespace aura::app
