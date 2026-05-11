#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura::service {

// ============================================================================
// Pipe security utilities (Phase 4)
//
// The AuraShell named pipe is protected by a DACL that restricts connections
// to the SYSTEM account (the service itself) and interactively-logged-in
// users (the UI app). All other principals are implicitly denied.
//
// SDDL used: D:P(A;;GA;;;SY)(A;;GRGW;;;IU)
//   D:P  — DACL, Protected (inheritance disabled)
//   SY   — SYSTEM: GENERIC_ALL
//   IU   — Interactive Users: GENERIC_READ + GENERIC_WRITE
// ============================================================================

// SDDL strings — constexpr so they live in read-only data, zero overhead.
inline constexpr wchar_t const* PIPE_SDDL_SECURE =
    L"D:P(A;;GA;;;SY)(A;;GRGW;;;IU)";

// A deny-all SDDL (protected DACL, no ACEs) — used in unit tests to verify
// that unauthorized connection attempts correctly receive ERROR_ACCESS_DENIED.
inline constexpr wchar_t const* PIPE_SDDL_DENY_ALL = L"D:P";

// ============================================================================
// buildPipeSecurityAttributes
//
// Converts an SDDL string into a populated SECURITY_ATTRIBUTES ready to pass
// to CreateNamedPipeW.  The security descriptor bytes are stored in sdBuffer;
// sa.lpSecurityDescriptor points into sdBuffer and MUST outlive sa.
//
// Returns true on success.  On failure sa is zeroed and sdBuffer is cleared.
// ============================================================================
[[nodiscard]] bool buildPipeSecurityAttributes(
    wchar_t const*          sddl,
    SECURITY_ATTRIBUTES&    sa,
    std::vector<uint8_t>&   sdBuffer
) noexcept;

// Convenience wrapper — uses PIPE_SDDL_SECURE.
[[nodiscard]] bool buildSecurePipeAttributes(
    SECURITY_ATTRIBUTES&  sa,
    std::vector<uint8_t>& sdBuffer
) noexcept;

// ============================================================================
// Privilege / elevation queries
// ============================================================================

// Returns true if the current process token has the elevated privilege flag.
// A service registered to run as SYSTEM always returns true here.
[[nodiscard]] bool isCurrentProcessElevated() noexcept;

// Returns true if the named privilege (e.g., SE_DEBUG_NAME) is held by the
// current process token.
[[nodiscard]] bool hasPrivilege(std::wstring_view privilegeName) noexcept;

// Returns the SID string (e.g., "S-1-5-18") for the current process token.
// Returns an empty string on failure.
[[nodiscard]] std::wstring getCurrentProcessSidString() noexcept;

}  // namespace aura::service
