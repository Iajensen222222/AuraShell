#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <thread>
#include <chrono>
#include <memory>
#include <windows.h>

// Use the real headers so the class layout matches the linked implementation.
// The old hand-rolled forward declarations had wrong/missing private members
// (ODR violation) which caused stack corruption and spurious test failures.
#include "named_pipe_server.h"
#include "named_pipe_client.h"
#include "message_types.h"

// All types (Message, HandshakePayload, NamedPipeServer, NamedPipeClient)
// are now provided by the included headers above.

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
            // Must call waitForClient first; receiveMessage on an unaccepted pipe
            // returns immediately with m_clientCurrentlyConnected == false.
            bool clientArrived = server.waitForClient(3000);
            REQUIRE(clientArrived);

            // Client is now connected but will not send any data.
            // receiveMessage should wait the full 500ms before timing out.
            Message msg;
            auto start = std::chrono::high_resolution_clock::now();
            bool received = server.receiveMessage(msg, 500);
            auto elapsed = std::chrono::high_resolution_clock::now() - start;

            REQUIRE(!received);

            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            REQUIRE(ms >= 400);
            REQUIRE(ms <= 700);
        });

        // Give server thread a moment to enter waitForClient
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Connect but send nothing — server should timeout in receiveMessage
        NamedPipeClient client;
        client.connect(3000);

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

        // Named pipes don't push a disconnect notification to the client —
        // the client learns of disconnection only on the next send/receive.
        // Attempt a send to trigger the detection.
        Message pingMsg;
        pingMsg.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        client.sendMessage(pingMsg, 500);  // expected to fail; updates m_connected

        // After the failed send, client should report disconnected
        REQUIRE(!client.isConnected());
    }
}
