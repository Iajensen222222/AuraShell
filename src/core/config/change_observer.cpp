#include "change_observer.h"

#include "logging/logger.h"

namespace aura::config {

bool ConfigChangeObserver::watch(const std::wstring& directory,
                                  const std::wstring& filename,
                                  std::function<void()> onChanged) {
    if (m_running.load()) return true; // already watching

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) return false;

    m_running.store(true);
    m_thread = std::thread(&ConfigChangeObserver::watcherThreadProc,
                           this, directory, filename, std::move(onChanged));

    // Convert wstring filename to UTF-8 for the logger.
    int const needed = WideCharToMultiByte(CP_UTF8, 0,
        filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1,
                            &narrow[0], needed, nullptr, nullptr);
    aura::logging::Logger::getInstance().info("config",
        "ConfigChangeObserver: watching " + narrow);
    return true;
}

void ConfigChangeObserver::stop() {
    if (!m_running.exchange(false)) return;
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
}

bool ConfigChangeObserver::isWatching() const {
    return m_running.load(std::memory_order_relaxed);
}

void ConfigChangeObserver::watcherThreadProc(std::wstring directory,
                                              std::wstring filename,
                                              std::function<void()> cb) {
    HANDLE hDir = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        aura::logging::Logger::getInstance().error("config",
            "ConfigChangeObserver: cannot open directory");
        m_running.store(false);
        return;
    }

    // Aligned buffer for FILE_NOTIFY_INFORMATION structs (4KB is plenty for
    // a single-file watch on a small config directory).
    alignas(DWORD) uint8_t buf[4096];
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    HANDLE handles[2] = { ov.hEvent, m_stopEvent };

    DWORD lastFireTick = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        ResetEvent(ov.hEvent);
        DWORD bytesReturned = 0;
        BOOL ok = ReadDirectoryChangesW(
            hDir, buf, sizeof(buf), FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned, &ov, nullptr);

        if (!ok && GetLastError() != ERROR_IO_PENDING) break;

        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) break; // stop event
        if (waitResult != WAIT_OBJECT_0) break;      // unexpected

        GetOverlappedResult(hDir, &ov, &bytesReturned, FALSE);
        if (bytesReturned == 0) continue;

        // Walk the FILE_NOTIFY_INFORMATION chain and look for our filename.
        const uint8_t* p = buf;
        bool matched = false;
        for (;;) {
            const auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
            std::wstring changed(fni->FileName, fni->FileNameLength / sizeof(wchar_t));

            if (_wcsicmp(changed.c_str(), filename.c_str()) == 0) {
                matched = true;
            }

            if (fni->NextEntryOffset == 0) break;
            p += fni->NextEntryOffset;
        }

        if (matched) {
            // Debounce: only fire if DEBOUNCE_MS has elapsed since last fire.
            DWORD now = GetTickCount();
            if (now - lastFireTick >= DEBOUNCE_MS) {
                lastFireTick = now;
                try { cb(); } catch (...) {}
                aura::logging::Logger::getInstance().info("config",
                    "ConfigChangeObserver: config.json changed — reloaded");
            }
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(hDir);
}

} // namespace aura::config
