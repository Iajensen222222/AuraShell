#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

#include "message_types.h"

namespace aura::ipc {

// ============================================================================
// Named Pipe Client - Connects to IPC server
// ============================================================================
//
// The client connects to the server's named pipe and exchanges messages.
// It handles reconnection logic and timeout handling.
//
// Usage:
//   NamedPipeClient client;
//   if (client.connect(3000)) {
//       Message request;
//       request.messageType = MessageType::HANDSHAKE_REQUEST;
//       if (client.sendMessage(request)) {
//           Message response;
//           if (client.receiveMessage(response)) {
//               // Process response
//           }
//       }
//       client.disconnect();
//   }

class NamedPipeClient {
public:
    static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\AuraShell_Control";
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;
    static constexpr uint32_t DEFAULT_RETRIES = 2;

    NamedPipeClient();
    ~NamedPipeClient();

    // Disable copy operations
    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    // Connection management

    // Connect to the server (blocks until connected, timeout, or max retries)
    // timeoutMs: timeout per connection attempt
    // maxRetries: number of retries if initial connection fails
    // Returns: true on success, false if cannot connect
    bool connect(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS,
                 uint32_t maxRetries = DEFAULT_RETRIES);

    // Check if currently connected to server
    bool isConnected() const;

    // Disconnect from server
    // Returns: true on success, false if already disconnected
    bool disconnect();

    // Message exchange (blocking operations)

    // Send a message to server (blocks until sent or timeout)
    // Returns: true on success, false on error (connection lost)
    bool sendMessage(const Message& msg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    // Receive a message from server (blocks until received or timeout)
    // Returns: true on success, false on timeout/error
    bool receiveMessage(Message& outMsg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

private:
    HANDLE m_pipe;          // Named pipe handle
    bool m_connected;       // Connection state
    uint32_t m_retryCount;  // Current retry count for connection attempts
};

} // namespace aura::ipc
