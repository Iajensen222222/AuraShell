// tests/integration/test_ipc_live.cpp
//
// OA-3: Live IPC end-to-end test.
// Launches AuraShellService.exe --console as a child process, connects via
// NamedPipeClient, verifies HANDSHAKE and QUERY_STATE, then disconnects cleanly.
//
// Tagged [live] — requires AuraShellService.exe to be present at the path
// returned by ServiceExePath(). Skipped automatically in headless/CI when the
// binary is absent.

#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <chrono>
#include <string>
#include <thread>

#include "named_pipe_client.h"
#include "message_types.h"

namespace {

// Returns the expected path to AuraShellService.exe relative to this binary.
std::wstring ServiceExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    // Strip the test binary filename to get the bin directory.
    auto pos = path.rfind(L'\\');
    if (pos != std::wstring::npos) path.resize(pos + 1);
    return path + L"AuraShellService.exe";
}

struct ServiceProcess {
    HANDLE hProcess{INVALID_HANDLE_VALUE};
    HANDLE hThread{INVALID_HANDLE_VALUE};

    bool start(std::wstring const& exePath) {
        std::wstring cmdLine = L"\"" + exePath + L"\" --console";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }
        hProcess = pi.hProcess;
        hThread  = pi.hThread;
        return true;
    }

    void stop() {
        if (hProcess != INVALID_HANDLE_VALUE) {
            TerminateProcess(hProcess, 0);
            WaitForSingleObject(hProcess, 3000);
            CloseHandle(hProcess);
            CloseHandle(hThread);
            hProcess = INVALID_HANDLE_VALUE;
            hThread  = INVALID_HANDLE_VALUE;
        }
    }

    ~ServiceProcess() { stop(); }
};

// Wait up to timeoutMs for the named pipe to become connectable.
bool waitForPipe(uint32_t timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE h = CreateFileW(
            aura::ipc::NamedPipeClient::PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

}  // namespace

// ============================================================================
// Live IPC handshake test
// ============================================================================

TEST_CASE("Live IPC: HANDSHAKE_REQUEST -> HANDSHAKE_RESPONSE from child service",
          "[integration][live][ipc]")
{
    std::wstring const exePath = ServiceExePath();

    // Skip if the service binary isn't present (headless CI, build-only run).
    if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        int const _n = WideCharToMultiByte(CP_UTF8, 0,
            exePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string _path(static_cast<size_t>(_n > 0 ? _n - 1 : 0), '\0');
        if (_n > 0)
            WideCharToMultiByte(CP_UTF8, 0, exePath.c_str(), -1,
                                &_path[0], _n, nullptr, nullptr);
        WARN("AuraShellService.exe not found at " + _path +
             " -- skipping live IPC test");
        return;
    }

    ServiceProcess svc;
    REQUIRE(svc.start(exePath));

    // Wait up to 3 s for the pipe to be ready.
    REQUIRE(waitForPipe(3000));

    aura::ipc::NamedPipeClient client;
    REQUIRE(client.connect(2000, /*maxRetries=*/3));

    // ── HANDSHAKE ──
    aura::ipc::Message req{};
    req.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::HANDSHAKE_REQUEST);
    req.sequenceNumber = 1;
    req.payloadSize    = 0;

    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(client.sendMessage(req, 2000));

    aura::ipc::Message resp{};
    REQUIRE(client.receiveMessage(resp, 2000));
    auto t1 = std::chrono::steady_clock::now();

    double const handshakeMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    INFO("Handshake RTT: " << handshakeMs << " ms");
    CHECK(handshakeMs < 200.0);
    CHECK(resp.messageType ==
          static_cast<uint32_t>(aura::ipc::MessageType::HANDSHAKE_RESPONSE));

    const auto* hp = resp.getPayload<aura::ipc::HandshakePayload>();
    REQUIRE(hp != nullptr);
    CHECK(hp->clientVersion == 0x0400);

    // ── QUERY_STATE ──
    aura::ipc::Message qreq{};
    qreq.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::QUERY_STATE);
    qreq.sequenceNumber = 2;
    qreq.payloadSize    = 0;

    REQUIRE(client.sendMessage(qreq, 2000));

    aura::ipc::Message qresp{};
    REQUIRE(client.receiveMessage(qresp, 2000));

    CHECK(qresp.messageType ==
          static_cast<uint32_t>(aura::ipc::MessageType::STATUS_REPORT));

    const auto* qr = qresp.getPayload<aura::ipc::QueryStateResponse>();
    REQUIRE(qr != nullptr);
    CHECK(qr->serviceVersion == 0x0400);
    // Service was just started so uptime must be very short (< 30 s).
    CHECK(qr->uptimeSeconds < 30u);

    // ── Graceful ACK disconnect ──
    aura::ipc::Message ack{};
    ack.messageType    = static_cast<uint32_t>(aura::ipc::MessageType::ACK);
    ack.sequenceNumber = 3;
    ack.payloadSize    = 0;
    client.sendMessage(ack, 1000);

    client.disconnect();
    svc.stop();
}
