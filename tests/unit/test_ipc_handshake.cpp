#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <thread>
#include <chrono>
#include <memory>
#include <windows.h>

// Forward declarations for IPC classes
namespace aura::ipc {

enum class MessageType : uint32_t {
    HANDSHAKE_REQUEST = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,
    CONFIG_RELOAD = 0x0010,
    CONFIG_APPLY = 0x0011,
    THEME_CHANGE = 0x0020,
    QUERY_STATE = 0x0030,
    ENABLE_FEATURE = 0x0040,
    DISABLE_FEATURE = 0x0041,
};

struct Message {
    uint32_t messageType;
    uint32_t sequenceNumber;
    uint32_t payloadSize;
    uint32_t reserved;
    uint8_t payload[2048];

    Message() : messageType(0), sequenceNumber(0), payloadSize(0), reserved(0) {
        std::memset(payload, 0, sizeof(payload));
    }

    bool isValid() const {
        return messageType != 0 && payloadSize <= 2048;
    }

    template<typename T>
    T* getPayload() {
        if (payloadSize < sizeof(T)) return nullptr;
        return reinterpret_cast<T*>(payload);
    }

    template<typename T>
    void setPayload(const T& data) {
        if (sizeof(T) > 2048) return;
        std::memcpy(payload, &data, sizeof(T));
        payloadSize = sizeof(T);
    }
};

struct HandshakePayload {
    uint32_t clientPID;
    uint32_t clientVersion;
    uint32_t capabilities;
};

// ============================================================================
// Named Pipe Server Interface
// ============================================================================

class NamedPipeServer {
public:
    static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\AuraShell_Control";
    static constexpr uint32_t PIPE_BUFFER_SIZE = 4096;
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;

    NamedPipeServer();
    ~NamedPipeServer();

    // Lifecycle
    bool initialize();
    bool shutdown();
    bool isRunning() const;

    // Message handling (blocking, call from dedicated thread)
    bool waitForClient(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);
    bool receiveMessage(Message& outMsg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);
    bool sendMessage(const Message& msg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    // Cleanup
    void disconnectClient();

private:
    HANDLE m_pipe;
    HANDLE m_clientConnected;
    bool m_running;
    DWORD m_clientPID;
};

// ============================================================================
// Named Pipe Client Interface
// ============================================================================

class NamedPipeClient {
public:
    static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\AuraShell_Control";
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;
    static constexpr uint32_t DEFAULT_RETRIES = 2;

    NamedPipeClient();
    ~NamedPipeClient();

    // Connection
    bool connect(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS, uint32_t maxRetries = DEFAULT_RETRIES);
    bool isConnected() const;
    bool disconnect();

    // Message exchange
    bool sendMessage(const Message& msg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);
    bool receiveMessage(Message& outMsg, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

private:
    HANDLE m_pipe;
    bool m_connected;
};

} // namespace aura::ipc

// ============================================================================
// TESTS: Named Pipe Server Creation & Lifecycle
// ============================================================================

TEST_CASE("NamedPipeServer::Initialization", "[ipc][handshake][server]") {
    using namespace aura::ipc;

    SECTION("Server initializes successfully") {
        NamedPipeServer server;

        REQUIRE(server.initialize());
        REQUIRE(server.isRunning());

        server.shutdown();
    }

    SECTION("Server can be shut down") {
        NamedPipeServer server;

        REQUIRE(server.initialize());
        REQUIRE(server.isRunning());

        bool result = server.shutdown();
        REQUIRE(result);
        REQUIRE(!server.isRunning());
    }

    SECTION("Multiple initialize/shutdown cycles work") {
        NamedPipeServer server;

        for (int i = 0; i < 3; i++) {
            REQUIRE(server.initialize());
            REQUIRE(server.isRunning());
            REQUIRE(server.shutdown());
            REQUIRE(!server.isRunning());
        }
    }

    SECTION("Pipe name is correct") {
        using namespace aura::ipc;
        REQUIRE(NamedPipeServer::PIPE_NAME == std::wstring(L"\\\\.\\pipe\\AuraShell_Control"));
    }
}

// ============================================================================
// TESTS: Named Pipe Client Connection
// ============================================================================

TEST_CASE("NamedPipeClient::Connection", "[ipc][handshake][client]") {
    using namespace aura::ipc;

    SECTION("Client connects to running server") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        // Server waits for client in separate thread
        bool clientConnected = false;
        std::thread serverThread([&server, &clientConnected]() {
            if (server.waitForClient(10000)) {
                clientConnected = true;
            }
        });

        // Give server thread time to start listening
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Client connects
        NamedPipeClient client;
        bool connected = client.connect(3000);

        serverThread.join();
        server.shutdown();

        REQUIRE(connected);
        REQUIRE(clientConnected);
    }

    SECTION("Client connection fails when server not running") {
        NamedPipeClient client;

        // No server running
        bool connected = client.connect(500);  // Short timeout

        REQUIRE(!connected);
        REQUIRE(!client.isConnected());
    }

    SECTION("Client can disconnect") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        std::thread serverThread([&server]() {
            server.waitForClient(5000);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(3000));
        REQUIRE(client.isConnected());

        bool disconnected = client.disconnect();
        REQUIRE(disconnected);
        REQUIRE(!client.isConnected());

        serverThread.join();
        server.shutdown();
    }

    SECTION("Client connection retries on failure") {
        NamedPipeClient client;

        // Attempt connection with retries (server not running)
        bool connected = client.connect(100, 2);  // 2 retries, short timeout

        REQUIRE(!connected);
    }
}

// ============================================================================
// TESTS: Handshake Message Exchange (Request/Response)
// ============================================================================

TEST_CASE("NamedPipeHandshake::RequestResponse", "[ipc][handshake][communication]") {
    using namespace aura::ipc;

    SECTION("Client sends HANDSHAKE_REQUEST and receives HANDSHAKE_RESPONSE") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        bool handshakeSuccess = false;
        uint32_t responseSequence = 0;

        std::thread serverThread([&server, &handshakeSuccess, &responseSequence]() {
            if (server.waitForClient(5000)) {
                Message request;
                if (server.receiveMessage(request, 5000)) {
                    // Verify request
                    if (request.messageType == static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST)) {
                        // Send response with matching sequence number
                        Message response;
                        response.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
                        response.sequenceNumber = request.sequenceNumber;  // Match request
                        response.payloadSize = 0;

                        if (server.sendMessage(response, 5000)) {
                            handshakeSuccess = true;
                            responseSequence = response.sequenceNumber;
                        }
                    }
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(3000));

        // Send handshake request
        Message request;
        request.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        request.sequenceNumber = 1;
        HandshakePayload payload;
        payload.clientPID = GetCurrentProcessId();
        payload.clientVersion = 0x00010000;
        payload.capabilities = 0xFF;
        request.setPayload(payload);

        REQUIRE(client.sendMessage(request, 3000));

        // Receive response
        Message response;
        REQUIRE(client.receiveMessage(response, 3000));

        REQUIRE(response.messageType == static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE));
        REQUIRE(response.sequenceNumber == request.sequenceNumber);  // Sequence must match

        client.disconnect();
        serverThread.join();
        server.shutdown();

        REQUIRE(handshakeSuccess);
    }

    SECTION("Sequence number is preserved in round-trip") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        uint32_t requestSeq = 42;
        uint32_t responseSeq = 0;

        std::thread serverThread([&server, &responseSeq]() {
            if (server.waitForClient(5000)) {
                Message msg;
                if (server.receiveMessage(msg, 5000)) {
                    msg.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
                    server.sendMessage(msg, 5000);
                    responseSeq = msg.sequenceNumber;
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(3000));

        Message request;
        request.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        request.sequenceNumber = requestSeq;

        REQUIRE(client.sendMessage(request, 3000));

        Message response;
        REQUIRE(client.receiveMessage(response, 3000));

        REQUIRE(response.sequenceNumber == requestSeq);

        client.disconnect();
        serverThread.join();
        server.shutdown();

        REQUIRE(responseSeq == requestSeq);
    }
}

// ============================================================================
// TESTS: Timeout Handling
// ============================================================================

TEST_CASE("NamedPipeHandshake::TimeoutHandling", "[ipc][handshake][timeout]") {
    using namespace aura::ipc;

    SECTION("Client respects connection timeout") {
        NamedPipeClient client;

        auto start = std::chrono::high_resolution_clock::now();
        bool connected = client.connect(500);  // 500ms timeout
        auto elapsed = std::chrono::high_resolution_clock::now() - start;

        REQUIRE(!connected);

        // Should timeout around 500ms (allow 100ms margin)
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        REQUIRE(ms >= 400);
        REQUIRE(ms <= 700);
    }

    SECTION("Server respects receive timeout") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        std::thread serverThread([&server]() {
            Message msg;
            auto start = std::chrono::high_resolution_clock::now();
            bool received = server.receiveMessage(msg, 500);  // 500ms timeout
            auto elapsed = std::chrono::high_resolution_clock::now() - start;

            REQUIRE(!received);

            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            REQUIRE(ms >= 400);
            REQUIRE(ms <= 700);
        });

        // Connect but don't send anything
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        NamedPipeClient client;
        client.connect(1000);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        serverThread.join();
        server.shutdown();
    }

    SECTION("Default timeout is 5 seconds") {
        REQUIRE(NamedPipeServer::DEFAULT_TIMEOUT_MS == 5000);
        REQUIRE(NamedPipeClient::DEFAULT_TIMEOUT_MS == 5000);
    }
}

// ============================================================================
// TESTS: Message Round-Trip Latency
// ============================================================================

TEST_CASE("NamedPipeHandshake::Latency", "[ipc][handshake][performance]") {
    using namespace aura::ipc;

    SECTION("Single message round-trip completes in < 50ms") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        uint64_t totalLatency = 0;

        std::thread serverThread([&server, &totalLatency]() {
            if (server.waitForClient(10000)) {
                Message request;
                if (server.receiveMessage(request, 5000)) {
                    Message response;
                    response.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
                    response.sequenceNumber = request.sequenceNumber;
                    server.sendMessage(response, 5000);
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(3000));

        Message request;
        request.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        request.sequenceNumber = 1;

        auto start = std::chrono::high_resolution_clock::now();

        REQUIRE(client.sendMessage(request, 3000));

        Message response;
        REQUIRE(client.receiveMessage(response, 3000));

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        client.disconnect();
        serverThread.join();
        server.shutdown();

        // Requirement: < 50ms for single round-trip (allowing margin for system delays)
        REQUIRE(ms < 50);
    }

    SECTION("Multiple round-trips average < 10ms per message") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        const int NUM_MESSAGES = 10;
        std::vector<uint64_t> latencies;

        std::thread serverThread([&server, NUM_MESSAGES]() {
            if (server.waitForClient(10000)) {
                for (int i = 0; i < NUM_MESSAGES; i++) {
                    Message request;
                    if (server.receiveMessage(request, 5000)) {
                        Message response;
                        response.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
                        response.sequenceNumber = request.sequenceNumber;
                        server.sendMessage(response, 5000);
                    }
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(3000));

        for (int i = 0; i < NUM_MESSAGES; i++) {
            Message request;
            request.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
            request.sequenceNumber = i + 1;

            auto start = std::chrono::high_resolution_clock::now();

            REQUIRE(client.sendMessage(request, 3000));

            Message response;
            REQUIRE(client.receiveMessage(response, 3000));

            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            latencies.push_back(ms);
        }

        client.disconnect();
        serverThread.join();
        server.shutdown();

        // Calculate average latency
        uint64_t totalLatency = 0;
        for (auto lat : latencies) {
            totalLatency += lat;
        }
        uint64_t avgLatency = totalLatency / NUM_MESSAGES;

        // Average should be < 10ms (per PLAN.md requirement)
        REQUIRE(avgLatency < 10);
    }
}

// ============================================================================
// TESTS: Concurrent Client Handling
// ============================================================================

TEST_CASE("NamedPipeHandshake::ConcurrentClients", "[ipc][handshake][concurrency]") {
    using namespace aura::ipc;

    SECTION("Server accepts multiple sequential clients") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        const int NUM_CLIENTS = 3;
        int successCount = 0;

        for (int i = 0; i < NUM_CLIENTS; i++) {
            bool clientSuccess = false;

            std::thread serverThread([&server, &clientSuccess]() {
                if (server.waitForClient(5000)) {
                    Message request;
                    if (server.receiveMessage(request, 5000)) {
                        Message response;
                        response.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
                        response.sequenceNumber = request.sequenceNumber;
                        if (server.sendMessage(response, 5000)) {
                            clientSuccess = true;
                        }
                    }
                }
                server.disconnectClient();
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            NamedPipeClient client;
            if (client.connect(2000)) {
                Message request;
                request.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
                request.sequenceNumber = i + 1;

                if (client.sendMessage(request, 2000)) {
                    Message response;
                    if (client.receiveMessage(response, 2000)) {
                        clientSuccess = true;
                    }
                }
                client.disconnect();
            }

            serverThread.join();

            if (clientSuccess) {
                successCount++;
            }
        }

        server.shutdown();

        REQUIRE(successCount == NUM_CLIENTS);
    }
}

// ============================================================================
// TESTS: Error Handling & Edge Cases
// ============================================================================

TEST_CASE("NamedPipeHandshake::ErrorHandling", "[ipc][handshake][errors]") {
    using namespace aura::ipc;

    SECTION("Server handles disconnected client gracefully") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        std::thread serverThread([&server]() {
            if (server.waitForClient(5000)) {
                // Client will disconnect before sending message
                Message msg;
                bool received = server.receiveMessage(msg, 2000);
                // Should timeout or return false gracefully
                REQUIRE(!received);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(2000));
        client.disconnect();  // Disconnect immediately without sending

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        serverThread.join();
        server.shutdown();
    }

    SECTION("Client detects server disconnection") {
        NamedPipeServer server;
        REQUIRE(server.initialize());

        std::thread serverThread([&server]() {
            if (server.waitForClient(5000)) {
                // Server shuts down without responding
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        NamedPipeClient client;
        REQUIRE(client.connect(2000));

        // Try to send message after server disconnects
        Message msg;
        msg.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        msg.sequenceNumber = 1;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        serverThread.join();
        server.shutdown();

        // After shutdown, client should not be connected
        REQUIRE(!client.isConnected());
    }
}
