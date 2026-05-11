#pragma once

#include <cstdint>

#include "named_pipe_client.h"
#include "message_types.h"
#include "theme_model.h"

namespace aura::app {

// ============================================================================
// AppClient — typed IPC wrapper for the Config App → AuraShellService channel.
//
// Lifecycle:
//   connect() → queryState() / pushTheme() → disconnect()
//
// Thread safety: not thread-safe; call from a single UI/worker thread.
// ============================================================================

class AppClient {
public:
    enum class ConnectResult : uint8_t {
        Connected,
        ServiceNotRunning,
        AccessDenied,
        Unknown,
    };

    AppClient();
    ~AppClient();

    AppClient(AppClient const&)            = delete;
    AppClient& operator=(AppClient const&) = delete;

    // Connect to the service and complete the HANDSHAKE exchange.
    // timeoutMs applies to both the pipe wait and each message exchange.
    [[nodiscard]] ConnectResult connect(uint32_t timeoutMs = 2000);

    void disconnect();

    [[nodiscard]] bool isConnected() const noexcept;

    // Send QUERY_STATE and wait for the STATUS_REPORT response.
    [[nodiscard]] bool queryState(aura::ipc::QueryStateResponse& out);

    // Push a theme to the service (fire-and-consume-ack).
    [[nodiscard]] bool pushTheme(ThemeConfig const& theme);

private:
    aura::ipc::NamedPipeClient m_pipe;
    uint32_t                   m_nextSeq{1};
};

}  // namespace aura::app
