#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <cstring>
#include <chrono>

// Mock IPC message structures (before implementation)
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
    uint32_t reserved;  // For alignment
    uint8_t payload[2048];

    // Constructor
    Message() : messageType(0), sequenceNumber(0), payloadSize(0), reserved(0) {
        std::memset(payload, 0, sizeof(payload));
    }

    // Validation
    bool isValid() const {
        return messageType != 0 && payloadSize <= 2048;
    }

    // Payload access
    template<typename T>
    T* getPayload() {
        if (payloadSize < sizeof(T)) return nullptr;
        return reinterpret_cast<T*>(payload);
    }

    template<typename T>
    const T* getPayload() const {
        if (payloadSize < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(payload);
    }

    // Payload serialization
    template<typename T>
    void setPayload(const T& data) {
        static_assert(sizeof(T) <= 2048, "Payload too large");
        std::memcpy(payload, &data, sizeof(T));
        payloadSize = sizeof(T);
    }
};

struct HandshakePayload {
    uint32_t clientPID;
    uint32_t clientVersion;
    uint32_t capabilities;
};

struct ThemeChangePayload {
    wchar_t themeName[256];
    uint32_t colorCount;
    uint32_t animationSpeed;  // Percentage
};

} // namespace aura::ipc

// ============================================================================
// TESTS: IPC Message Construction & Validation
// ============================================================================

TEST_CASE("IpcMessage::Construction", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("Message default constructor initializes to zero") {
        Message msg;

        REQUIRE(msg.messageType == 0);
        REQUIRE(msg.sequenceNumber == 0);
        REQUIRE(msg.payloadSize == 0);
    }

    SECTION("Message payload is zero-initialized") {
        Message msg;

        // All payload bytes should be zero
        for (size_t i = 0; i < sizeof(msg.payload); i++) {
            REQUIRE(msg.payload[i] == 0);
        }
    }
}

TEST_CASE("IpcMessage::Validation", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("Uninitialized message is invalid") {
        Message msg;

        REQUIRE(!msg.isValid());
    }

    SECTION("Message with type and valid payload is valid") {
        Message msg;
        msg.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        msg.payloadSize = 12;

        REQUIRE(msg.isValid());
    }

    SECTION("Message with oversized payload is invalid") {
        Message msg;
        msg.messageType = static_cast<uint32_t>(MessageType::CONFIG_RELOAD);
        msg.payloadSize = 3000;  // Exceeds max

        REQUIRE(!msg.isValid());
    }

    SECTION("Message with zero type is invalid") {
        Message msg;
        msg.messageType = 0;
        msg.payloadSize = 10;

        REQUIRE(!msg.isValid());
    }
}

// ============================================================================
// TESTS: IPC Payload Serialization
// ============================================================================

TEST_CASE("IpcMessage::PayloadSerialization", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("setPayload stores data correctly") {
        Message msg;
        HandshakePayload hs;
        hs.clientPID = 12345;
        hs.clientVersion = 0x00010000;
        hs.capabilities = 0xFF;

        msg.setPayload(hs);

        REQUIRE(msg.payloadSize == sizeof(HandshakePayload));
    }

    SECTION("getPayload retrieves stored data") {
        Message msg;
        HandshakePayload hs;
        hs.clientPID = 67890;
        hs.clientVersion = 0x00020000;
        hs.capabilities = 0xAA;

        msg.setPayload(hs);

        const HandshakePayload* retrieved = msg.getPayload<HandshakePayload>();

        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->clientPID == 67890);
        REQUIRE(retrieved->clientVersion == 0x00020000);
        REQUIRE(retrieved->capabilities == 0xAA);
    }

    SECTION("getPayload returns null for insufficient payload size") {
        Message msg;
        msg.payloadSize = 5;  // Too small

        const HandshakePayload* result = msg.getPayload<HandshakePayload>();

        REQUIRE(result == nullptr);
    }

    SECTION("ThemeChangePayload serialization and retrieval") {
        Message msg;
        ThemeChangePayload theme;
        wcscpy_s(theme.themeName, 256, L"Neon Gamer");
        theme.colorCount = 8;
        theme.animationSpeed = 150;

        msg.setPayload(theme);

        const ThemeChangePayload* retrieved = msg.getPayload<ThemeChangePayload>();

        REQUIRE(retrieved != nullptr);
        REQUIRE(wcscmp(retrieved->themeName, L"Neon Gamer") == 0);
        REQUIRE(retrieved->colorCount == 8);
        REQUIRE(retrieved->animationSpeed == 150);
    }
}

// ============================================================================
// TESTS: IPC Message Types
// ============================================================================

TEST_CASE("IpcMessage::MessageTypes", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("All message types defined correctly") {
        // Just verify they compile and have expected values
        REQUIRE(static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST) == 0x0001);
        REQUIRE(static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE) == 0x0002);
        REQUIRE(static_cast<uint32_t>(MessageType::CONFIG_RELOAD) == 0x0010);
        REQUIRE(static_cast<uint32_t>(MessageType::THEME_CHANGE) == 0x0020);
        REQUIRE(static_cast<uint32_t>(MessageType::QUERY_STATE) == 0x0030);
    }

    SECTION("Message type values are unique") {
        Message msg1, msg2;
        msg1.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_REQUEST);
        msg2.messageType = static_cast<uint32_t>(MessageType::THEME_CHANGE);

        REQUIRE(msg1.messageType != msg2.messageType);
    }
}

// ============================================================================
// TESTS: IPC Sequence Numbers
// ============================================================================

TEST_CASE("IpcMessage::SequenceNumbers", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("Sequence number can be set") {
        Message msg;
        msg.sequenceNumber = 42;

        REQUIRE(msg.sequenceNumber == 42);
    }

    SECTION("Sequence numbers can match request to response") {
        Message request, response;
        request.messageType = static_cast<uint32_t>(MessageType::QUERY_STATE);
        request.sequenceNumber = 123;

        response.messageType = static_cast<uint32_t>(MessageType::HANDSHAKE_RESPONSE);
        response.sequenceNumber = 123;  // Match request sequence

        REQUIRE(request.sequenceNumber == response.sequenceNumber);
    }

    SECTION("Sequence number overflow handled") {
        Message msg;
        msg.sequenceNumber = UINT32_MAX;

        // Can increment with wrapping
        msg.sequenceNumber = (msg.sequenceNumber + 1);

        REQUIRE(msg.sequenceNumber == 0);  // Wrapped around
    }
}

// ============================================================================
// TESTS: IPC Message Size Constraints
// ============================================================================

TEST_CASE("IpcMessage::SizeConstraints", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("Message header size is predictable") {
        Message msg;

        // Should be: 4 uint32s + 2048 bytes payload = 2064 bytes
        REQUIRE(sizeof(Message) == (4 * sizeof(uint32_t)) + 2048);
    }

    SECTION("Maximum payload size is 2048 bytes") {
        Message msg;
        msg.messageType = static_cast<uint32_t>(MessageType::CONFIG_APPLY);
        msg.payloadSize = 2048;

        REQUIRE(msg.isValid());
    }

    SECTION("Payload exceeding max size fails validation") {
        Message msg;
        msg.messageType = static_cast<uint32_t>(MessageType::CONFIG_APPLY);
        msg.payloadSize = 2049;

        REQUIRE(!msg.isValid());
    }
}

// ============================================================================
// TESTS: IPC Message Copying & Transfer
// ============================================================================

TEST_CASE("IpcMessage::Copying", "[ipc][message]") {
    using namespace aura::ipc;

    SECTION("Message can be copied with memcpy") {
        Message original;
        original.messageType = static_cast<uint32_t>(MessageType::THEME_CHANGE);
        original.sequenceNumber = 42;
        original.payloadSize = 10;

        Message copy;
        std::memcpy(&copy, &original, sizeof(Message));

        REQUIRE(copy.messageType == original.messageType);
        REQUIRE(copy.sequenceNumber == original.sequenceNumber);
        REQUIRE(copy.payloadSize == original.payloadSize);
    }

    SECTION("Message payload preserved through copy") {
        Message original;
        HandshakePayload hs;
        hs.clientPID = 999;
        hs.clientVersion = 1;
        hs.capabilities = 7;

        original.setPayload(hs);

        Message copy;
        std::memcpy(&copy, &original, sizeof(Message));

        const HandshakePayload* copiedPayload = copy.getPayload<HandshakePayload>();

        REQUIRE(copiedPayload != nullptr);
        REQUIRE(copiedPayload->clientPID == 999);
    }
}
