#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace aura::config {

// Watches a directory for changes to a specific filename using
// ReadDirectoryChangesW (async overlapped I/O). When the watched file is
// modified, a 300ms debounce timer fires a callback so that rapid successive
// saves (common in text editors) produce only one notification.
//
// Usage:
//   ConfigChangeObserver obs;
//   obs.watch(L"C:\\Users\\...\\AuraShell", L"config.json",
//             [](){ SettingsManager::getInstance().load(); });
//   obs.stop();  // on shutdown
class ConfigChangeObserver {
public:
    ConfigChangeObserver() = default;
    ~ConfigChangeObserver() { stop(); }
    ConfigChangeObserver(const ConfigChangeObserver&) = delete;
    ConfigChangeObserver& operator=(const ConfigChangeObserver&) = delete;

    // Begin watching `directory` for changes to `filename`.
    // `onChanged` is called on the watcher thread after the debounce window.
    // Returns false if the directory cannot be opened.
    bool watch(const std::wstring& directory,
               const std::wstring& filename,
               std::function<void()> onChanged);

    // Stop watching and join the watcher thread.
    void stop();

    bool isWatching() const;

private:
    void watcherThreadProc(std::wstring directory, std::wstring filename,
                           std::function<void()> cb);

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    HANDLE            m_stopEvent{nullptr};

    static constexpr DWORD DEBOUNCE_MS = 300;
};

} // namespace aura::config
