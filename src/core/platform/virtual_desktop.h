#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <windows.h>
#include <objbase.h>
#include <ShObjIdl.h>

namespace aura::platform {

// Detects virtual desktop switches using the public IVirtualDesktopManager COM
// API (Windows 10 1607+). Because there is no public event interface, detection
// is achieved by polling GetWindowDesktopId(GetForegroundWindow()) every 500ms
// and firing a callback whenever the returned GUID changes.
//
// Usage:
//   auto& vd = VirtualDesktopDetector::getInstance();
//   vd.initialize();
//   vd.subscribeDesktopChange([](GUID prev, GUID next) { /* re-apply theme */ });
class VirtualDesktopDetector {
public:
    static VirtualDesktopDetector& getInstance();

    // Start the COM object and the polling thread.
    // Safe to call multiple times — no-op if already initialized.
    bool initialize();

    // Join the polling thread and release the COM object.
    void shutdown();

    bool isInitialized() const;

    // -----------------------------------------------------------------------
    // Queries (thread-safe)
    // -----------------------------------------------------------------------

    // GUID of the virtual desktop that currently has focus.
    // Returns GUID_NULL if COM is unavailable or no foreground window exists.
    GUID getCurrentDesktopId() const;

    // True if hwnd is on the desktop that currently has focus.
    // Returns true on COM error (fail open — don't break window management).
    bool isWindowOnCurrentDesktop(HWND hwnd) const;

    // -----------------------------------------------------------------------
    // Events
    // -----------------------------------------------------------------------

    // Callback signature: void(GUID previousDesktop, GUID newDesktop)
    using DesktopChangeCallback = std::function<void(GUID, GUID)>;

    // Register a callback invoked on the polling thread when the desktop changes.
    void subscribeDesktopChange(DesktopChangeCallback cb);

private:
    VirtualDesktopDetector() = default;
    ~VirtualDesktopDetector() { shutdown(); }
    VirtualDesktopDetector(const VirtualDesktopDetector&) = delete;
    VirtualDesktopDetector& operator=(const VirtualDesktopDetector&) = delete;

    void pollThreadProc();

    IVirtualDesktopManager*   m_pVDM{nullptr};  // COM, released in shutdown()
    mutable GUID              m_currentId{};

    std::thread               m_pollThread;
    std::atomic<bool>         m_running{false};
    bool                      m_initialized{false};

    mutable SRWLOCK           m_callbackLock = SRWLOCK_INIT;
    std::vector<DesktopChangeCallback> m_callbacks;

    static constexpr DWORD POLL_INTERVAL_MS = 500;
};

} // namespace aura::platform
