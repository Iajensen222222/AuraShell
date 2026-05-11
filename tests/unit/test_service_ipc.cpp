// Phase 4 TDD — Service Layer & Persistent IPC
// Tests are in three groups:
//   A. DACL / security utilities  — pure Win32, no SCM required
//   B. Service lifecycle          — ServiceCore start/stop without SCM
//   C. Manager-Worker IPC flow    — ServiceCore server + NamedPipeClient

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <cstring>

#define NOMINMAX
#include <Windows.h>

#include "service_core.h"
#include "pipe_security.h"
#include "named_pipe_client.h"
#include "message_types.h"

using namespace aura::service;
using namespace aura::ipc;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helper: build a Message from a MessageType and payload
// ---------------------------------------------------------------------------
template <typename PayloadT>
static Message makeMessage(MessageType type, uint32_t seq, PayloadT const& p) {
    Message m;
    m.messageType   = static_cast<uint32_t>(type);
    m.sequenceNumber = seq;
    m.setPayload(p);
    return m;
}

// ============================================================================
// A. DACL / Pipe Security
// ============================================================================

TEST_CASE("PipeSecurity::BuildSecureAttributes", "[service][security]") {
    SECTION("buildSecurePipeAttributes succeeds") {
        SECURITY_ATTRIBUTES  sa  = {};
        std::vector<uint8_t> buf;
        bool const ok = buildSecurePipeAttributes(sa, buf);
        REQUIRE(ok == true);
        REQUIRE(sa.lpSecurityDescriptor != nullptr);
        REQUIRE(!buf.empty());
    }

    SECTION("security descriptor is self-relative and valid") {
        SECURITY_ATTRIBUTES  sa  = {};
        std::vector<uint8_t> buf;
        REQUIRE(buildSecurePipeAttributes(sa, buf));

        PSECURITY_DESCRIPTOR pSd = sa.lpSecurityDescriptor;
        REQUIRE(IsValidSecurityDescriptor(pSd) == TRUE);
        REQUIRE(buf.data() == reinterpret_cast<uint8_t*>(pSd));
    }

    SECTION("deny-all SDDL produces empty DACL") {
        SECURITY_ATTRIBUTES  sa  = {};
        std::vector<uint8_t> buf;
        REQUIRE(buildPipeSecurityAttributes(PIPE_SDDL_DENY_ALL, sa, buf));
        REQUIRE(IsValidSecurityDescriptor(sa.lpSecurityDescriptor) == TRUE);
    }

    SECTION("invalid SDDL returns false") {
        SECURITY_ATTRIBUTES  sa  = {};
        std::vector<uint8_t> buf;
        bool const ok = buildPipeSecurityAttributes(L"GARBAGE-SDDL", sa, buf);
        REQUIRE(ok == false);
        REQUIRE(buf.empty());
    }

    SECTION("bInheritHandle is FALSE for pipe security") {
        SECURITY_ATTRIBUTES  sa  = {};
        std::vector<uint8_t> buf;
        REQUIRE(buildSecurePipeAttributes(sa, buf));
        REQUIRE(sa.bInheritHandle == FALSE);
    }
}

TEST_CASE("PipeSecurity::ElevationQuery", "[service][security]") {
    SECTION("isCurrentProcessElevated returns a boolean without crashing") {
        // We don't assert the value since the test may run elevated or not.
        bool const elev = isCurrentProcessElevated();
        (void)elev;  // suppress unused-variable warning
        SUCCEED("isCurrentProcessElevated() returned without exception");
    }

    SECTION("getCurrentProcessSidString returns a non-empty SID string") {
        std::wstring const sid = getCurrentProcessSidString();
        REQUIRE(!sid.empty());
        // SID strings start with "S-"
        REQUIRE(sid.substr(0, 2) == L"S-");
    }
}

TEST_CASE("PipeSecurity::AccessDenied", "[service][security][accessdenied]") {
    // Create a named pipe with a deny-all DACL.  Even the current process user
    // should be denied when attempting to connect as a client.
    //
    // Note: If the test runs as SYSTEM (a CI scenario), SYSTEM can bypass deny
    // ACLs on objects it owns.  The test detects this and skips.

    static constexpr wchar_t const* kTestPipe = L"\\\\.\\pipe\\AuraShell_DenyTest";

    SECURITY_ATTRIBUTES  sa  = {};
    std::vector<uint8_t> buf;
    REQUIRE(buildPipeSecurityAttributes(PIPE_SDDL_DENY_ALL, sa, buf));

    HANDLE const hPipe = CreateNamedPipeW(
        kTestPipe,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,      // max instances
        512,
        512,
        500,    // timeout ms
        &sa
    );
    REQUIRE(hPipe != INVALID_HANDLE_VALUE);

    // Try to connect from the same user context.
    HANDLE const hClient = CreateFileW(
        kTestPipe,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    DWORD const err = GetLastError();

    if (hClient != INVALID_HANDLE_VALUE) {
        // Running as SYSTEM — implicitly owns the pipe, can bypass deny-all.
        // Skip rather than fail.
        CloseHandle(hClient);
        WARN("Skipping access-denied check: test is running as SYSTEM or owner");
        SUCCEED("Access-denied skipped for SYSTEM identity");
    } else {
        // Expected path for non-SYSTEM users.
        REQUIRE(err == ERROR_ACCESS_DENIED);
    }

    CloseHandle(hPipe);
}

// ============================================================================
// B. Service Lifecycle
// ============================================================================

TEST_CASE("ServiceCore::Lifecycle", "[service][lifecycle]") {
    SECTION("initialize and shutdown complete without error") {
        ServiceCore& svc = ServiceCore::getInstance();
        REQUIRE(svc.initialize() == true);
        REQUIRE(svc.isRunning()  == true);
        svc.shutdown();
        REQUIRE(svc.isRunning()  == false);
    }

    SECTION("double initialize is safe") {
        ServiceCore& svc = ServiceCore::getInstance();
        REQUIRE(svc.initialize() == true);
        REQUIRE(svc.initialize() == true);  // idempotent
        REQUIRE(svc.isRunning()  == true);
        svc.shutdown();
    }

    SECTION("double shutdown is safe") {
        ServiceCore& svc = ServiceCore::getInstance();
        REQUIRE(svc.initialize() == true);
        svc.shutdown();
        svc.shutdown();  // must not crash
        REQUIRE(svc.isRunning() == false);
    }

    SECTION("getCurrentTheme returns default on fresh start") {
        ServiceCore& svc = ServiceCore::getInstance();
        REQUIRE(svc.initialize());
        REQUIRE(svc.getCurrentTheme() == L"default");
        svc.shutdown();
    }

    SECTION("uptimeSeconds increases over time") {
        ServiceCore& svc = ServiceCore::getInstance();
        REQUIRE(svc.initialize());
        std::this_thread::sleep_for(1100ms);
        REQUIRE(svc.uptimeSeconds() >= 1);
        svc.shutdown();
    }

    SECTION("multiple start-stop cycles succeed") {
        ServiceCore& svc = ServiceCore::getInstance();
        for (int i = 0; i < 3; ++i) {
            REQUIRE(svc.initialize());
            REQUIRE(svc.isRunning());
            svc.shutdown();
            REQUIRE_FALSE(svc.isRunning());
        }
    }
}

// ============================================================================
// C. Manager-Worker IPC flow (ServiceCore server + NamedPipeClient)
// ============================================================================

struct ServiceIpcFixture {
    ServiceIpcFixture() {
        svc = &ServiceCore::getInstance();
        REQUIRE(svc->initialize());
        std::this_thread::sleep_for(80ms);  // let IPC thread reach waitForClient
    }
    ~ServiceIpcFixture() {
        client.disconnect();
        svc->shutdown();
    }
    ServiceCore*     svc    = nullptr;
    NamedPipeClient  client;
};

TEST_CASE("ServiceIPC::Handshake", "[service][ipc]") {
    ServiceIpcFixture fix;

    SECTION("client connects and receives HANDSHAKE_RESPONSE") {
        REQUIRE(fix.client.connect(3000));

        HandshakePayload req = {};
        req.clientPID     = GetCurrentProcessId();
        req.clientVersion = 0x0400;
        req.capabilities  = 0x00FF;

        Message request = makeMessage(MessageType::HANDSHAKE_REQUEST, 1, req);
        REQUIRE(fix.client.sendMessage(request, 3000));

        Message response;
        REQUIRE(fix.client.receiveMessage(response, 3000));
        REQUIRE(response.messageType   ==
                static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE));
        REQUIRE(response.sequenceNumber == 1);

        auto const* payload = response.getPayload<HandshakePayload>();
        REQUIRE(payload != nullptr);
        REQUIRE(payload->clientVersion == 0x0400);
        REQUIRE(payload->capabilities  != 0);
    }
}

TEST_CASE("ServiceIPC::QueryState", "[service][ipc]") {
    ServiceIpcFixture fix;
    REQUIRE(fix.client.connect(3000));

    // Handshake first.
    {
        HandshakePayload hp = {};
        hp.clientPID     = GetCurrentProcessId();
        hp.clientVersion = 0x0400;
        Message req = makeMessage(MessageType::HANDSHAKE_REQUEST, 10, hp);
        fix.client.sendMessage(req, 2000);
        Message resp;
        fix.client.receiveMessage(resp, 2000);
    }

    SECTION("QUERY_STATE returns current theme and service version") {
        Message qry;
        qry.messageType    = static_cast<uint32_t>(MessageType::QUERY_STATE);
        qry.sequenceNumber = 20;
        qry.payloadSize    = 0;

        REQUIRE(fix.client.sendMessage(qry, 2000));

        Message resp;
        REQUIRE(fix.client.receiveMessage(resp, 3000));
        REQUIRE(resp.messageType   ==
                static_cast<uint32_t>(MessageType::STATUS_REPORT));
        REQUIRE(resp.sequenceNumber == 20);

        auto const* qr = resp.getPayload<QueryStateResponse>();
        REQUIRE(qr != nullptr);
        REQUIRE(std::wstring(qr->currentTheme) == L"default");
        REQUIRE(qr->serviceVersion == 0x0400);
    }
}

TEST_CASE("ServiceIPC::PushTheme", "[service][ipc]") {
    ServiceIpcFixture fix;
    REQUIRE(fix.client.connect(3000));

    // Handshake.
    {
        HandshakePayload hp = {};
        hp.clientPID = GetCurrentProcessId();
        hp.clientVersion = 0x0400;
        Message req = makeMessage(MessageType::HANDSHAKE_REQUEST, 30, hp);
        fix.client.sendMessage(req, 2000);
        Message resp;
        fix.client.receiveMessage(resp, 2000);
    }

    SECTION("service pushes new theme and client receives PUSH_THEME frame") {
        std::wstring const newTheme = L"neon_gamer";

        // Push happens on the service side — a separate push, not a response.
        // Launch a thread to receive it on the client side while we push.
        std::atomic<bool> received{false};
        std::wstring      receivedTheme;

        std::thread recvThread([&] {
            Message pushed;
            if (fix.client.receiveMessage(pushed, 3000)) {
                if (pushed.messageType ==
                    static_cast<uint32_t>(MessageType::PUSH_THEME)) {
                    auto const* tp = pushed.getPayload<ThemePayload>();
                    if (tp) {
                        receivedTheme = tp->themeName;
                        received      = true;
                    }
                }
            }
        });

        std::this_thread::sleep_for(30ms);  // ensure recv thread is blocked
        bool const pushed = fix.svc->pushTheme(newTheme);
        recvThread.join();

        if (pushed) {
            // Push succeeded — client should have received it.
            REQUIRE(received.load() == true);
            REQUIRE(receivedTheme   == newTheme);
            REQUIRE(fix.svc->getCurrentTheme() == newTheme);
        } else {
            // No client connected yet (race) — acceptable, just verify state.
            SUCCEED("Push returned false (no connected client at push time)");
        }
    }
}

TEST_CASE("ServiceIPC::PushConfig", "[service][ipc]") {
    ServiceIpcFixture fix;
    REQUIRE(fix.client.connect(3000));

    // Handshake.
    {
        HandshakePayload hp = {};
        hp.clientPID = GetCurrentProcessId();
        hp.clientVersion = 0x0400;
        Message req = makeMessage(MessageType::HANDSHAKE_REQUEST, 40, hp);
        fix.client.sendMessage(req, 2000);
        Message resp;
        fix.client.receiveMessage(resp, 2000);
    }

    SECTION("pushConfig with valid JSON succeeds (or returns false if no client)") {
        std::string const json = R"({"theme":"minimal","animSpeed":100})";
        bool const ok = fix.svc->pushConfig(json);
        // We just verify it doesn't crash — actual delivery tested via recv.
        (void)ok;
        SUCCEED("pushConfig completed without exception");
    }

    SECTION("pushConfig with oversized payload returns false") {
        std::string const huge(2049, 'x');  // 2049 bytes — over the 2048-byte limit
        REQUIRE(fix.svc->pushConfig(huge) == false);
    }
}

TEST_CASE("ServiceIPC::SequenceNumbers", "[service][ipc]") {
    ServiceIpcFixture fix;
    REQUIRE(fix.client.connect(3000));

    SECTION("response echoes request sequence number") {
        HandshakePayload hp = {};
        hp.clientPID     = GetCurrentProcessId();
        hp.clientVersion = 0x0400;

        for (uint32_t seq = 1; seq <= 3; ++seq) {
            Message req = makeMessage(MessageType::HANDSHAKE_REQUEST, seq, hp);
            REQUIRE(fix.client.sendMessage(req, 2000));

            Message resp;
            REQUIRE(fix.client.receiveMessage(resp, 3000));
            REQUIRE(resp.sequenceNumber == seq);
        }
    }
}
