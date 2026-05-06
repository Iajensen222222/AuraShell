#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace aura::ipc {

// Forward declaration
struct Message;

// ============================================================================
// Named Pipe Server - Listens for IPC connections
// ============================================================================
//
// The server listens on a named pipe and handles incoming client connections.
// It processes message requests and sends responses. This is a blocking API
// designed to be called from a dedicated IPC handling thread (not the main thread).
//
// Usage:
//   NamedPipeServer server;
//   if (server.initialize()) {
//       std::thread ipcThread([&server]() {
//           while (server.isRunning()) {
//               if (server.waitForClient(5000)) {
//                   Message msg;
//                   if (server.receiveMessage(msg)) {
//                       // Process msg
//                       // Send response
//                       server.sendMessage(responseMsg);
//                   }
//                   server.disconnectClient();
//               }
//           }
//       });
//   }

class NamedPipeServer {
public:
    static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\AuraShell_Control";
    static constexpr uint32_t PIPE_BUFFER_SIZE = 4096;
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;

    NamedPipeServer();
    ~NamedPipeServer();

    // Disable copy operations
    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // Lifecycle management
    // Initialize the named pipe server (creates the listening pipe)
    bool initialize();

    // Shutdown the server (closes all connections)
    bool shutdown();

    // Check if server is running
    bool isRunning() const;

    // Message handling (blocking operations, call from dedicated thread)

    // Wait for a client to connect (blocks until connection or timeout)
    // Returns: true if client connected, false on timeout/error
    bool waitForClient(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    // Receive a message from connected client (blocks until message or timeout)
    // Returns: true on success, false on timeout/error
    bool receiveMessage(Message& outMsg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    // Send a message to connected client (blocks until sent or timeout)
    // Returns: true on success, false on error
    bool sendMessage(const Message& msg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    // Disconnect the currently connected client
    void disconnectClient();

    // Get the PID of the connected client (for logging/security)
    uint32_t getConnectedClientPID() const;

private:
    HANDLE m_pipe;                      // Named pipe handle
    HANDLE m_clientConnected;           // Event: client connection established
    bool m_running;                     // Server running state
    uint32_t m_clientPID;               // Connected client process ID
    bool m_clientCurrentlyConnected;    // Current connection status
};

} // namespace aura::ipc
