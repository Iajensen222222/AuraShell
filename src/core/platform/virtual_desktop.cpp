#include "virtual_desktop.h"

#include <vector>

#include "logging/logger.h"

// {AA509086-5CA9-4C25-8F95-589D3C07B48A}
static constexpr CLSID CLSID_VirtualDesktopManager_IMPL = {
    0xAA509086, 0x5CA9, 0x4C25,
    {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}
};

namespace aura::platform {

VirtualDesktopDetector& VirtualDesktopDetector::getInstance() {
    static VirtualDesktopDetector instance;
    return instance;
}

bool VirtualDesktopDetector::initialize() {
    if (m_initialized) return true;

    // COM must be initialized on the calling thread.
    // CoInitializeEx is idempotent per thread — safe to call even if the host
    // app already called it (returns S_FALSE, not a failure).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HRESULT hr = CoCreateInstance(
        CLSID_VirtualDesktopManager_IMPL,
        nullptr,
        CLSCTX_ALL,
        IID_IVirtualDesktopManager,
        reinterpret_cast<void**>(&m_pVDM));

    if (FAILED(hr) || !m_pVDM) {
        aura::logging::Logger::getInstance().warn("platform",
            "VirtualDesktopDetector: CoCreateInstance failed (0x" +
            [hr]{ char buf[16]; std::snprintf(buf,sizeof(buf),"%08X",static_cast<unsigned>(hr)); return std::string(buf); }()
            + ") — virtual desktop features disabled");
        return false;
    }

    // Capture initial desktop ID before starting the poll thread.
    HWND fg = GetForegroundWindow();
    if (fg) {
        m_pVDM->GetWindowDesktopId(fg, &m_currentId);
    }

    m_running.store(true);
    m_pollThread = std::thread(&VirtualDesktopDetector::pollThreadProc, this);

    m_initialized = true;
    aura::logging::Logger::getInstance().info("platform",
        "VirtualDesktopDetector initialized");
    return true;
}

void VirtualDesktopDetector::shutdown() {
    if (!m_initialized) return;

    m_running.store(false);
    if (m_pollThread.joinable()) m_pollThread.join();

    if (m_pVDM) {
        m_pVDM->Release();
        m_pVDM = nullptr;
    }

    m_initialized = false;
}

bool VirtualDesktopDetector::isInitialized() const {
    return m_initialized;
}

GUID VirtualDesktopDetector::getCurrentDesktopId() const {
    return m_currentId;
}

bool VirtualDesktopDetector::isWindowOnCurrentDesktop(HWND hwnd) const {
    if (!m_pVDM || !hwnd) return true;
    BOOL onCurrent = TRUE;
    m_pVDM->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrent);
    return onCurrent != FALSE;
}

void VirtualDesktopDetector::subscribeDesktopChange(DesktopChangeCallback cb) {
    AcquireSRWLockExclusive(&m_callbackLock);
    m_callbacks.push_back(std::move(cb));
    ReleaseSRWLockExclusive(&m_callbackLock);
}

void VirtualDesktopDetector::pollThreadProc() {
    // COM must be initialized on this thread independently.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    while (m_running.load(std::memory_order_relaxed)) {
        HWND fg = GetForegroundWindow();
        if (fg && m_pVDM) {
            GUID newId{};
            HRESULT hr = m_pVDM->GetWindowDesktopId(fg, &newId);

            if (SUCCEEDED(hr) &&
                !IsEqualGUID(newId, GUID_NULL) &&
                !IsEqualGUID(newId, m_currentId)) {

                GUID prev   = m_currentId;
                m_currentId = newId;

                // Fire all callbacks outside the lock to prevent deadlocks.
                std::vector<DesktopChangeCallback> snapshot;
                {
                    AcquireSRWLockShared(&m_callbackLock);
                    snapshot = m_callbacks;
                    ReleaseSRWLockShared(&m_callbackLock);
                }
                for (auto& cb : snapshot) {
                    try { cb(prev, newId); }
                    catch (...) {}
                }

                aura::logging::Logger::getInstance().info("platform",
                    "Virtual desktop switched");
            }
        }

        Sleep(POLL_INTERVAL_MS);
    }

    CoUninitialize();
}

} // namespace aura::platform
