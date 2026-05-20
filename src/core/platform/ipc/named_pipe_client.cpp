#include "named_pipe_client.h"
#include <windows.h>
#include <cstring>
#include <chrono>
#include <thread>
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
// NamedPipeClient Implementation
// ============================================================================

NamedPipeClient::NamedPipeClient()
    : m_pipe(INVALID_HANDLE_VALUE), m_connected(false), m_retryCount(0) {}

NamedPipeClient::~NamedPipeClient() {
    if (m_connected) {
        disconnect();
    }
}

bool NamedPipeClient::connect(uint32_t timeoutMs, uint32_t maxRetries) {
    // maxRetries is kept for API compatibility but timeoutMs is now the primary
    // constraint.  The loop retries until the deadline regardless of attempt count.
    (void)maxRetries;

    try {
        if (m_connected) {
            return true;
        }

        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        uint32_t attemptCount = 0;

        while (Clock::now() < deadline) {
            attemptCount++;

            HANDLE hPipe = CreateFileW(
                PIPE_NAME,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (hPipe != INVALID_HANDLE_VALUE) {
                DWORD pipeMode = PIPE_READMODE_MESSAGE;
                if (!SetNamedPipeHandleState(hPipe, &pipeMode, nullptr, nullptr)) {
                    spdlog::error("SetNamedPipeHandleState failed: 0x{:08X}", GetLastError());
                    CloseHandle(hPipe);
                    return false;
                }
                m_pipe = hPipe;
                m_connected = true;
                m_retryCount = attemptCount - 1;
                spdlog::info("NamedPipeClient connected after {} attempt(s)", attemptCount);
                return true;
            }

            DWORD err = GetLastError();

            auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()).count();
            if (remainingMs <= 0) break;

            if (err == ERROR_PIPE_BUSY) {
                // Server is busy — wait up to remaining time for a slot.
                DWORD waitMs = static_cast<DWORD>(std::min<long long>(remainingMs, 200));
                WaitNamedPipeW(PIPE_NAME, waitMs);
            } else if (err == ERROR_FILE_NOT_FOUND) {
                // Pipe doesn't exist yet — sleep a short interval and retry.
                DWORD sleepMs = static_cast<DWORD>(std::min<long long>(remainingMs, 50));
                spdlog::debug("Pipe not found, waiting {}ms (attempt {})", sleepMs, attemptCount);
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            } else {
                spdlog::error("CreateFileW failed: 0x{:08X}", err);
                return false;
            }
        }

        spdlog::warn("Failed to connect after {} attempt(s)", attemptCount);
        return false;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeClient::connect exception: {}", e.what());
        return false;
    }
}

bool NamedPipeClient::isConnected() const {
    return m_connected;
}

bool NamedPipeClient::disconnect() {
    try {
        if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) {
            return true;
        }

        FlushFileBuffers(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
        m_connected = false;
        spdlog::debug("NamedPipeClient disconnected");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("NamedPipeClient::disconnect exception: {}", e.what());
        return false;
    }
}

bool NamedPipeClient::sendMessage(const Message& msg, uint32_t timeoutMs) {
    try {
        if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) {
            spdlog::warn("sendMessage called on disconnected client");
            m_connected = false;
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
                    m_connected = false;
                    return false;
                }

                // Get overlap result
                if (!GetOverlappedResult(m_pipe, &ov, &bytesWritten, FALSE)) {
                    DWORD resultErr = GetLastError();
                    if (resultErr == ERROR_PIPE_NOT_CONNECTED) {
                        m_connected = false;
                        spdlog::info("Server disconnected");
                    } else {
                        spdlog::error("GetOverlappedResult failed: 0x{:08X}", resultErr);
                    }
                    return false;
                }
            } else if (err == ERROR_PIPE_NOT_CONNECTED) {
                m_connected = false;
                spdlog::info("Server disconnected during write");
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
        spdlog::error("NamedPipeClient::sendMessage exception: {}", e.what());
        m_connected = false;
        return false;
    }
}

bool NamedPipeClient::receiveMessage(Message& outMsg, uint32_t timeoutMs) {
    try {
        if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) {
            spdlog::warn("receiveMessage called on disconnected client");
            m_connected = false;
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
                    m_connected = false;
                    return false;
                }

                // Get overlap result
                if (!GetOverlappedResult(m_pipe, &ov, &bytesRead, FALSE)) {
                    DWORD resultErr = GetLastError();
                    if (resultErr == ERROR_PIPE_NOT_CONNECTED) {
                        m_connected = false;
                        spdlog::info("Server disconnected");
                    } else {
                        spdlog::error("GetOverlappedResult failed: 0x{:08X}", resultErr);
                    }
                    return false;
                }
            } else if (err == ERROR_PIPE_NOT_CONNECTED) {
                m_connected = false;
                spdlog::info("Server disconnected during read");
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
        spdlog::error("NamedPipeClient::receiveMessage exception: {}", e.what());
        m_connected = false;
        return false;
    }
}

} // namespace aura::ipc
