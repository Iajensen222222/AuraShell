// Sample 03: Named pipe IPC client — connects to AuraShellService, performs
// HANDSHAKE + QUERY_STATE, then disconnects. Documents the exact wire format
// for third-party integrations.
//
// Prerequisites: AuraShellService.exe must be running (--console or as SCM service).
// Run: .\Sample03_IpcClient.exe
//
// The wire format is defined in src/core/platform/ipc/message_types.h.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

// ── Minimal inline message_types (avoids pulling the full C++ library) ──────

enum class AuraMsg : uint32_t {
    HANDSHAKE_REQUEST  = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,
    QUERY_STATE        = 0x0030,
    ACK                = 0x0070,
};

#pragma pack(push, 1)
struct Frame {
    uint32_t msgType;
    uint32_t seqNum;
    uint32_t payloadSize;
    uint32_t reserved;
    uint8_t  payload[2048];
};
static_assert(sizeof(Frame) == 2064, "Frame must be 2064 bytes");

struct HandshakePayload {
    uint32_t clientPID;
    uint32_t clientVersion;
    uint32_t capabilities;
};

struct HandshakeResponse {
    uint32_t serviceVersion;
    uint32_t uptimeSeconds;
    bool     isElevated;
    uint8_t  _pad[3];
    wchar_t  message[128];
};

struct QueryStateResponse {
    wchar_t  currentTheme[256];
    uint32_t serviceVersion;
    uint32_t uptimeSeconds;
    bool     isElevated;
    uint8_t  _pad[3];
};
#pragma pack(pop)

// ── Helpers ──────────────────────────────────────────────────────────────────

static Frame makeFrame(AuraMsg type, const void* payload, uint32_t payloadSize) {
    Frame f{};
    f.msgType     = static_cast<uint32_t>(type);
    f.payloadSize = payloadSize;
    std::memcpy(f.payload, payload, payloadSize);
    return f;
}

static bool sendFrame(HANDLE pipe, const Frame& f) {
    DWORD written = 0;
    return WriteFile(pipe, &f, sizeof(f), &written, nullptr) && written == sizeof(f);
}

static bool recvFrame(HANDLE pipe, Frame& f) {
    DWORD read = 0;
    return ReadFile(pipe, &f, sizeof(f), &read, nullptr) && read == sizeof(f);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    constexpr wchar_t kPipe[] = L"\\\\.\\pipe\\AuraShellService";
    printf("AuraShell IPC client sample\n");
    printf("Connecting to %ls ...\n", kPipe);

    // Wait up to 5 seconds for the pipe to become available.
    if (!WaitNamedPipeW(kPipe, 5000)) {
        printf("ERROR: pipe not available (is AuraShellService running?)\n");
        return 1;
    }

    HANDLE pipe = CreateFileW(kPipe, GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        printf("ERROR: CreateFileW failed %lu\n", GetLastError());
        return 1;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    printf("Connected.\n\n");

    // ── HANDSHAKE ────────────────────────────────────────────────────────────
    HandshakePayload hReq{ GetCurrentProcessId(), 0x0600, 0 };
    Frame tx = makeFrame(AuraMsg::HANDSHAKE_REQUEST, &hReq, sizeof(hReq));
    sendFrame(pipe, tx);
    printf("[TX] HANDSHAKE_REQUEST  pid=%lu version=0x%04X\n",
           hReq.clientPID, hReq.clientVersion);

    Frame rx{};
    if (recvFrame(pipe, rx) &&
        static_cast<AuraMsg>(rx.msgType) == AuraMsg::HANDSHAKE_RESPONSE) {
        auto* r = reinterpret_cast<HandshakeResponse*>(rx.payload);
        printf("[RX] HANDSHAKE_RESPONSE version=0x%04X uptime=%us elevated=%s\n",
               r->serviceVersion, r->uptimeSeconds,
               r->isElevated ? "yes" : "no");
        if (r->message[0]) wprintf(L"     message: %ls\n", r->message);
    } else {
        printf("[RX] Unexpected response 0x%04X\n", rx.msgType);
    }

    // ── QUERY_STATE ──────────────────────────────────────────────────────────
    Frame qTx{};
    qTx.msgType = static_cast<uint32_t>(AuraMsg::QUERY_STATE);
    sendFrame(pipe, qTx);
    printf("\n[TX] QUERY_STATE\n");

    if (recvFrame(pipe, rx)) {
        auto* s = reinterpret_cast<QueryStateResponse*>(rx.payload);
        wprintf(L"[RX] STATE theme='%ls' uptime=%us elevated=%s\n",
                s->currentTheme, s->uptimeSeconds,
                s->isElevated ? "yes" : "no");
    }

    // ── ACK (graceful disconnect) ─────────────────────────────────────────────
    Frame ack{};
    ack.msgType = static_cast<uint32_t>(AuraMsg::ACK);
    sendFrame(pipe, ack);
    printf("\n[TX] ACK (disconnect)\n");

    CloseHandle(pipe);
    printf("Done.\n");
    return 0;
}
