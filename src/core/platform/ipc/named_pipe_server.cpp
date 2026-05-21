#include "named_pipe_server.h"
#include <windows.h>
#include <cstring>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace aura::ipc {

// ============================================================================
// RAII Helper: Automatic handle cleanup
// ============================================================================
class HandleGuard {
public:
    explicit HandleGuard(HANDLE h = INVALID_HANDLE_VALUE) : m_handle(h) {}

    ~HandleGuard() {
        if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
            CloseHandle(m_handle);
        }
    }

    HANDLE get() const { return m_handle; }
    HANDLE* addressof() { return &m_handle; }
    HANDLE release() {
        HANDLE h = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return h;
    }

    // Disable copy
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    // Allow move
    HandleGuard(HandleGuard&& other) noexcept : m_handle(other.release()) {}
    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
            m_handle = other.release();
        }
        return *this;
    }

private:
    HANDLE m_handle;
};

// ============================================================================
// NamedPipeServer Implementation
// ============================================================================

NamedPipeServer::NamedPipeServer()
    : m_pipe(INVALID_HANDLE_VALUE), m_clientConnected(nullptr),
      m_running(false), m_clientPID(0), m_clientCurrentlyConnected(false) {}

NamedPipeServer::~NamedPipeServer() {
    if (m_running) {
        shutdown();
    }
}

bool NamedPipeServer::initialize() {
    return initialize(nullptr);  // default: no security restriction
}

bool NamedPipeServer::initialize(SECURITY_ATTRIBUTES const* const pSa) {
    try {
        if (m_running) {
            return true;  // Already initialized
        }

        // Create named pipe with overlapped I/O for timeout handling.
        // pSa carries the DACL; nullptr = default (Everyone) security.
        HANDLE const hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,                // Max instances — one App connection at a time
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            DEFAULT_TIMEOUT_MS,
            const_cast<SECURITY_ATTRIBUTES*>(pSa)   // Win32 API is non-const
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            DWORD const err = GetLastError();
            spdlog::error("CreateNamedPipeW failed: 0x{:08X}", err);
            return false;
        }

        m_pipe    = hPipe;
        m_running = true;
        spdlog::info("NamedPipeServer initialized (DACL={})",
                     pSa ? "custom" : "default");
        return true;
    } catch (std::exception const& e) {
        spdlog::error("NamedPipeServer::initialize exception: {}", e.what());
        return false;
    }
}

bool NamedPipeServer::shutdown() {
    try {
        if (!m_running) {
            return true;
        }

        // Disconnect client if connected
        if (m_clientCurrentlyConnected) {
            DisconnectNamedPipe(m_pipe);
            m_clientCurrentlyConnected = false;
        }

        // Close pipe handle
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }

        m_running = false;
        spdlog::info("NamedPipeServer shutdown complete");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeServer::shutdown exception: {}", e.what());
        return false;
    }
}

bool NamedPipeServer::isRunning() const {
    return m_running;
}

bool NamedPipeServer::waitForClient(uint32_t timeoutMs) {
    try {
        if (!m_running || m_pipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Create event for connection notification
        HandleGuard hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (hEvent.get() == nullptr) {
            spdlog::error("CreateEventW failed: 0x{:08X}", GetLastError());
            return false;
        }

        // Prepare overlapped structure
        OVERLAPPED ov = {};
        ov.hEvent = hEvent.get();

        // Wait for client connection (non-blocking with overlapped I/O)
        BOOL connected = ConnectNamedPipe(m_pipe, &ov);

        if (!connected) {
            DWORD err = GetLastError();

            if (err == ERROR_IO_PENDING) {
                // Connection is pending, wait for it
                DWORD wait = WaitForSingleObject(hEvent.get(), timeoutMs);

                if (wait == WAIT_TIMEOUT) {
                    CancelIo(m_pipe);
                    spdlog::warn("waitForClient timeout after {}ms", timeoutMs);
                    return false;
                }

                if (wait != WAIT_OBJECT_0) {
                    DWORD waitErr = GetLastError();
                    spdlog::error("WaitForSingleObject failed: 0x{:08X}", waitErr);
                    CancelIo(m_pipe);
                    return false;
                }

                // Get overlap result
                DWORD transferred = 0;
                if (!GetOverlappedResult(m_pipe, &ov, &transferred, FALSE)) {
                    spdlog::error("GetOverlappedResult failed: 0x{:08X}", GetLastError());
                    return false;
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                // Client already connected (rare race condition)
                spdlog::debug("Client already connected");
            } else {
                spdlog::error("ConnectNamedPipe failed: 0x{:08X}", err);
                return false;
            }
        }

        // Get connected client PID for logging/security
        ULONG clientPID = 0;
        if (GetNamedPipeClientProcessId(m_pipe, &clientPID)) {
            m_clientPID = clientPID;
            spdlog::debug("Client connected: PID={}", clientPID);
        }

        m_clientCurrentlyConnected = true;
        return true;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeServer::waitForClient exception: {}", e.what());
        return false;
    }
}

bool NamedPipeServer::receiveMessage(Message& outMsg, uint32_t timeoutMs) {
    try {
        if (!m_running || !m_clientCurrentlyConnected) {
            return false;
        }

        // Create event for read completion
        HandleGuard hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (hEvent.get() == nullptr) {
            spdlog::error("CreateEventW failed: 0x{:08X}", GetLastError());
            return false;
        }

        // Prepare overlapped structure
        OVERLAPPED ov = {};
        ov.hEvent = hEvent.get();

        // Issue async read
        DWORD bytesRead = 0;
        BOOL readResult = ReadFile(
            m_pipe,
            &outMsg,
            sizeof(Message),
            &bytesRead,
            &ov
        );

        if (!readResult) {
            DWORD err = GetLastError();

            if (err == ERROR_IO_PENDING) {
                // Read pending, wait for completion
                DWORD wait = WaitForSingleObject(hEvent.get(), timeoutMs);

                if (wait == WAIT_TIMEOUT) {
                    CancelIo(m_pipe);
                    spdlog::warn("receiveMessage timeout after {}ms", timeoutMs);
                    return false;
                }

                if (wait != WAIT_OBJECT_0) {
                    DWORD waitErr = GetLastError();
                    spdlog::error("WaitForSingleObject failed: 0x{:08X}", waitErr);
                    CancelIo(m_pipe);
                    m_clientCurrentlyConnected = false;
                    return false;
                }

                // Get overlap result
                if (!GetOverlappedResult(m_pipe, &ov, &bytesRead, FALSE)) {
                    DWORD resultErr = GetLastError();
                    if (resultErr == ERROR_PIPE_NOT_CONNECTED) {
                        m_clientCurrentlyConnected = false;
                        spdlog::info("Client disconnected");
                    } else {
                        spdlog::error("GetOverlappedResult failed: 0x{:08X}", resultErr);
                    }
                    return false;
                }
            } else if (err == ERROR_PIPE_NOT_CONNECTED) {
                m_clientCurrentlyConnected = false;
                spdlog::info("Client disconnected during read");
                return false;
            } else {
                spdlog::error("ReadFile failed: 0x{:08X}", err);
                return false;
            }
        }

        // Validate read size
        if (bytesRead != sizeof(Message)) {
            spdlog::warn("Incomplete message read: {} bytes (expected {})",
                        bytesRead, sizeof(Message));
            return false;
        }

        // Validate message
        if (outMsg.messageType == 0) {
            spdlog::warn("Invalid message: type is 0");
            return false;
        }

        if (outMsg.payloadSize > 2048) {
            spdlog::warn("Invalid message: payload size {} exceeds max 2048",
                        outMsg.payloadSize);
            return false;
        }

        spdlog::debug("Message received: type=0x{:04X}, seq={}, size={}",
                     outMsg.messageType, outMsg.sequenceNumber, outMsg.payloadSize);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeServer::receiveMessage exception: {}", e.what());
        return false;
    }
}

bool NamedPipeServer::sendMessage(const Message& msg, uint32_t timeoutMs) {
    try {
        if (!m_running || !m_clientCurrentlyConnected) {
            return false;
        }

        // Create event for write completion
        HandleGuard hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (hEvent.get() == nullptr) {
            spdlog::error("CreateEventW failed: 0x{:08X}", GetLastError());
            return false;
        }

        // Prepare overlapped structure
        OVERLAPPED ov = {};
        ov.hEvent = hEvent.get();

        // Issue async write
        DWORD bytesWritten = 0;
        BOOL writeResult = WriteFile(
            m_pipe,
            &msg,
            sizeof(Message),
            &bytesWritten,
            &ov
        );

        if (!writeResult) {
            DWORD err = GetLastError();

            if (err == ERROR_IO_PENDING) {
                // Write pending, wait for completion
                DWORD wait = WaitForSingleObject(hEvent.get(), timeoutMs);

                if (wait == WAIT_TIMEOUT) {
                    CancelIo(m_pipe);
                    spdlog::warn("sendMessage timeout after {}ms", timeoutMs);
                    return false;
                }

                if (wait != WAIT_OBJECT_0) {
                    DWORD waitErr = GetLastError();
                    spdlog::error("WaitForSingleObject failed: 0x{:08X}", waitErr);
                    CancelIo(m_pipe);
                    m_clientCurrentlyConnected = false;
                    return false;
                }

                // Get overlap result
                if (!GetOverlappedResult(m_pipe, &ov, &bytesWritten, FALSE)) {
                    DWORD resultErr = GetLastError();
                    if (resultErr == ERROR_PIPE_NOT_CONNECTED) {
                        m_clientCurrentlyConnected = false;
                        spdlog::info("Client disconnected");
                    } else {
                        spdlog::error("GetOverlappedResult failed: 0x{:08X}", resultErr);
                    }
                    return false;
                }
            } else if (err == ERROR_PIPE_NOT_CONNECTED) {
                m_clientCurrentlyConnected = false;
                spdlog::info("Client disconnected during write");
                return false;
            } else {
                spdlog::error("WriteFile failed: 0x{:08X}", err);
                return false;
            }
        }

        // Validate write size
        if (bytesWritten != sizeof(Message)) {
            spdlog::warn("Incomplete message written: {} bytes (expected {})",
                        bytesWritten, sizeof(Message));
            return false;
        }

        spdlog::debug("Message sent: type=0x{:04X}, seq={}, size={}",
                     msg.messageType, msg.sequenceNumber, msg.payloadSize);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeServer::sendMessage exception: {}", e.what());
        return false;
    }
}

void NamedPipeServer::disconnectClient() {
    try {
        if (m_pipe != INVALID_HANDLE_VALUE && m_clientCurrentlyConnected) {
            FlushFileBuffers(m_pipe);
            DisconnectNamedPipe(m_pipe);
            m_clientCurrentlyConnected = false;
            m_clientPID = 0;
            spdlog::debug("Client disconnected");
        }
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeServer::disconnectClient exception: {}", e.what());
    }
}

uint32_t NamedPipeServer::getConnectedClientPID() const {
    return m_clientPID;
}

bool NamedPipeServer::isClientConnected() const {
    return m_clientCurrentlyConnected;
}

} // namespace aura::ipc
