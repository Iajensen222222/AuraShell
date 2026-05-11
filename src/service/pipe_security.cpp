#include "pipe_security.h"

#include <Windows.h>
#include <sddl.h>      // ConvertStringSecurityDescriptorToSecurityDescriptorW
#include <winnt.h>
#include <aclapi.h>

#pragma comment(lib, "advapi32.lib")

namespace aura::service {

// ============================================================================
// buildPipeSecurityAttributes
// ============================================================================

bool buildPipeSecurityAttributes(
    wchar_t const*        sddl,
    SECURITY_ATTRIBUTES&  sa,
    std::vector<uint8_t>& sdBuffer
) noexcept {
    sa       = {};
    sdBuffer.clear();

    PSECURITY_DESCRIPTOR pSd = nullptr;
    ULONG sdSize             = 0;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl,
            SDDL_REVISION_1,
            &pSd,
            &sdSize
        )) {
        return false;
    }

    // Copy the security descriptor into our managed buffer so the caller owns
    // the lifetime (LocalFree's pSd immediately after).
    sdBuffer.resize(sdSize);
    std::memcpy(sdBuffer.data(), pSd, sdSize);
    LocalFree(pSd);

    sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sdBuffer.data();
    sa.bInheritHandle       = FALSE;
    return true;
}

bool buildSecurePipeAttributes(
    SECURITY_ATTRIBUTES&  sa,
    std::vector<uint8_t>& sdBuffer
) noexcept {
    return buildPipeSecurityAttributes(PIPE_SDDL_SECURE, sa, sdBuffer);
}

// ============================================================================
// Privilege / elevation helpers
// ============================================================================

bool isCurrentProcessElevated() noexcept {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_ELEVATION elev   = {};
    DWORD           cbSize = sizeof(elev);
    bool const      ok     = GetTokenInformation(
        hToken, TokenElevation, &elev, cbSize, &cbSize
    ) == TRUE;

    CloseHandle(hToken);
    return ok && (elev.TokenIsElevated != 0);
}

bool hasPrivilege(std::wstring_view const privilegeName) noexcept {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    LUID luid = {};
    if (!LookupPrivilegeValueW(nullptr, privilegeName.data(), &luid)) {
        CloseHandle(hToken);
        return false;
    }

    // Get the token privilege list size first.
    DWORD cbSize = 0;
    GetTokenInformation(hToken, TokenPrivileges, nullptr, 0, &cbSize);

    std::vector<uint8_t> buf(cbSize, 0);
    bool held = false;

    if (GetTokenInformation(hToken, TokenPrivileges, buf.data(), cbSize, &cbSize)) {
        auto const* const pPrivs =
            reinterpret_cast<TOKEN_PRIVILEGES const*>(buf.data());
        for (DWORD i = 0; i < pPrivs->PrivilegeCount; ++i) {
            auto const& p = pPrivs->Privileges[i];
            if (p.Luid.LowPart  == luid.LowPart &&
                p.Luid.HighPart == luid.HighPart &&
                (p.Attributes & SE_PRIVILEGE_ENABLED) != 0) {
                held = true;
                break;
            }
        }
    }

    CloseHandle(hToken);
    return held;
}

std::wstring getCurrentProcessSidString() noexcept {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return {};
    }

    DWORD cbSize = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &cbSize);
    std::vector<uint8_t> buf(cbSize, 0);

    std::wstring result;
    if (GetTokenInformation(hToken, TokenUser, buf.data(), cbSize, &cbSize)) {
        auto const* const pUser =
            reinterpret_cast<TOKEN_USER const*>(buf.data());
        wchar_t* pSidStr = nullptr;
        if (ConvertSidToStringSidW(pUser->User.Sid, &pSidStr) && pSidStr) {
            result = pSidStr;
            LocalFree(pSidStr);
        }
    }

    CloseHandle(hToken);
    return result;
}

}  // namespace aura::service
