#include "hover_detector.h"
#include "logging/logger.h"
#include <chrono>
#include <algorithm>

namespace aura::taskbar {

// ============================================================================
// Singleton Instance
// ============================================================================

HoverDetector& HoverDetector::getInstance() {
    static HoverDetector instance;
    return instance;
}

// ============================================================================
// Constructor & Destructor
// ============================================================================

HoverDetector::HoverDetector() {
    aura::logging::Logger::getInstance().debug("hover", "HoverDetector constructed");
}

HoverDetector::~HoverDetector() {
    if (m_running) {
        shutdown();
    }
    aura::logging::Logger::getInstance().debug("hover", "HoverDetector destroyed");
}

// ============================================================================
// Lifecycle Methods
// ============================================================================

void HoverDetector::initialize() {
    if (m_initialized) {
        aura::logging::Logger::getInstance().debug("hover", "HoverDetector already initialized");
        return;
    }

    try {
        aura::logging::Logger::getInstance().info("hover", "HoverDetector::initialize()");

        // Get reference to TaskbarController
        m_taskbarController = &TaskbarController::getInstance();

        // Subscribe to taskbar state changes
        m_taskbarController->registerStateChangeCallback(
            [this](const TaskbarState& state) {
                this->onTaskbarStateChanged(state);
            }
        );

        // Cache initial taskbar state
        const auto& currentState = m_taskbarController->getCurrentState();
        onTaskbarStateChanged(currentState);

        // Start background polling thread
        m_running = true;
        m_pollingThread = std::make_unique<std::thread>(&HoverDetector::pollingThreadProc, this);

        m_initialized = true;
        aura::logging::Logger::getInstance().info("hover", "HoverDetector initialized successfully");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error(
            "hover",
            std::string("Initialize failed: ") + e.what()
        );
        m_initialized = false;
    }
}

void HoverDetector::shutdown() {
    if (!m_initialized) {
        return;
    }

    try {
        aura::logging::Logger::getInstance().info("hover", "HoverDetector::shutdown()");

        // Stop polling thread
        m_running = false;

        if (m_pollingThread && m_pollingThread->joinable()) {
            m_pollingThread->join();
        }

        m_pollingThread.reset();

        // Clear hover state
        {
            std::unique_lock<std::shared_mutex> lock(m_hoverStateMutex);
            m_currentHoveredIcon = nullptr;
            m_currentHoveredIconIndex = UINT32_MAX;
        }

        // Clear caches
        {
            std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
            m_iconCache.clear();
            m_taskbarRect = {0, 0, 0, 0};
        }

        // Clear callbacks
        {
            std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
            m_hoverEnterCallbacks.clear();
            m_hoverExitCallbacks.clear();
        }

        m_initialized = false;
        aura::logging::Logger::getInstance().info("hover", "HoverDetector shutdown complete");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error(
            "hover",
            std::string("Shutdown failed: ") + e.what()
        );
    }
}

bool HoverDetector::isInitialized() const {
    return m_initialized;
}

// ============================================================================
// Mouse Position Query Methods
// ============================================================================

bool HoverDetector::isMouseOverTaskbar(const POINT& mousePos) const {
    std::shared_lock<std::shared_mutex> lock(m_cacheMutex);

    return (mousePos.x >= m_taskbarRect.left && mousePos.x <= m_taskbarRect.right &&
            mousePos.y >= m_taskbarRect.top && mousePos.y <= m_taskbarRect.bottom);
}

const TaskbarIconInfo* HoverDetector::getHoveredIcon(const POINT& mousePos) const {
    std::shared_lock<std::shared_mutex> lock(m_cacheMutex);

    uint32_t hoveredIndex = hitTestIcons(mousePos);

    if (hoveredIndex != UINT32_MAX && hoveredIndex < m_iconCache.size()) {
        return &m_iconCache[hoveredIndex];
    }

    return nullptr;
}

// ============================================================================
// Real-Time Mouse Tracking
// ============================================================================

void HoverDetector::updateMousePosition(const POINT& mousePos) {
    if (!m_initialized) {
        return;
    }

    // Check if mouse is over taskbar
    bool isOverTaskbar = isMouseOverTaskbar(mousePos);

    // Hit-test icons if over taskbar
    uint32_t hoveredIconIndex = UINT32_MAX;
    if (isOverTaskbar) {
        std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
        hoveredIconIndex = hitTestIcons(mousePos);
    }

    // Update hover state with debouncing
    {
        std::unique_lock<std::shared_mutex> lock(m_hoverStateMutex);

        // Determine if state has changed
        uint32_t previousHoveredIconIndex = m_currentHoveredIconIndex;
        bool isEntering = (hoveredIconIndex != UINT32_MAX && previousHoveredIconIndex == UINT32_MAX);
        bool isExiting = (hoveredIconIndex == UINT32_MAX && previousHoveredIconIndex != UINT32_MAX);
        bool isSwitching = (hoveredIconIndex != UINT32_MAX &&
                           previousHoveredIconIndex != UINT32_MAX &&
                           hoveredIconIndex != previousHoveredIconIndex);

        // Check debouncing
        if ((isEntering || isExiting || isSwitching) && shouldProcessStateChange()) {
            m_currentHoveredIconIndex = hoveredIconIndex;

            // Update m_currentHoveredIcon pointer
            if (hoveredIconIndex != UINT32_MAX && hoveredIconIndex < m_iconCache.size()) {
                m_currentHoveredIcon = &m_iconCache[hoveredIconIndex];
            } else {
                m_currentHoveredIcon = nullptr;
            }

            // Trigger callbacks (outside lock to prevent deadlock)
            lock.unlock();

            if (isEntering || isSwitching) {
                // Invoke hover enter callbacks
                {
                    std::shared_lock<std::shared_mutex> callbackLock(m_callbackMutex);
                    if (hoveredIconIndex < m_iconCache.size()) {
                        const TaskbarIconInfo& icon = m_iconCache[hoveredIconIndex];
                        for (const auto& callback : m_hoverEnterCallbacks) {
                            try {
                                callback(icon);
                            } catch (const std::exception& e) {
                                aura::logging::Logger::getInstance().error(
                                    "hover",
                                    std::string("HoverEnter callback failed: ") + e.what()
                                );
                            }
                        }
                    }
                }
            }

            if (isExiting || isSwitching) {
                // Invoke hover exit callbacks
                {
                    std::shared_lock<std::shared_mutex> callbackLock(m_callbackMutex);
                    for (const auto& callback : m_hoverExitCallbacks) {
                        try {
                            callback();
                        } catch (const std::exception& e) {
                            aura::logging::Logger::getInstance().error(
                                "hover",
                                std::string("HoverExit callback failed: ") + e.what()
                            );
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Hover State Callbacks
// ============================================================================

void HoverDetector::subscribeHoverEnter(HoverEnterCallback callback) {
    std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
    m_hoverEnterCallbacks.push_back(callback);
}

void HoverDetector::subscribeHoverExit(HoverExitCallback callback) {
    std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
    m_hoverExitCallbacks.push_back(callback);
}

// ============================================================================
// Configuration Methods
// ============================================================================

void HoverDetector::setDebounceDelayMs(uint32_t delayMs) {
    m_debounceDelayMs = delayMs;
    aura::logging::Logger::getInstance().debug(
        "hover",
        std::string("Debounce delay set to ") + std::to_string(delayMs) + "ms"
    );
}

uint32_t HoverDetector::getDebounceDelayMs() const {
    return m_debounceDelayMs;
}

void HoverDetector::setPollingIntervalMs(uint32_t intervalMs) {
    m_pollingIntervalMs = intervalMs;
    aura::logging::Logger::getInstance().debug(
        "hover",
        std::string("Polling interval set to ") + std::to_string(intervalMs) + "ms"
    );
}

uint32_t HoverDetector::getPollingIntervalMs() const {
    return m_pollingIntervalMs;
}

// ============================================================================
// Internal State Access
// ============================================================================

RECT HoverDetector::getTaskbarBounds() const {
    std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
    return m_taskbarRect;
}

std::vector<TaskbarIconInfo> HoverDetector::getCachedIcons() const {
    std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
    return m_iconCache;
}

const TaskbarIconInfo* HoverDetector::getCurrentHoveredIcon() const {
    std::shared_lock<std::shared_mutex> lock(m_hoverStateMutex);
    return m_currentHoveredIcon;
}

// ============================================================================
// Diagnostic & Performance
// ============================================================================

std::string HoverDetector::getPerformanceStats() const {
    std::string stats;
    stats += "HoverDetector Performance Stats:\n";
    stats += "  Polling Interval: " + std::to_string(m_pollingIntervalMs) + "ms\n";
    stats += "  Debounce Delay: " + std::to_string(m_debounceDelayMs) + "ms\n";
    stats += "  Cached Icons: " + std::to_string(m_iconCache.size()) + "\n";
    stats += "  Hovering: " + std::string(m_currentHoveredIcon ? "Yes" : "No");
    return stats;
}

// ============================================================================
// Private Methods
// ============================================================================

void HoverDetector::pollingThreadProc() {
    aura::logging::Logger::getInstance().debug("hover", "Polling thread started");

    while (m_running) {
        try {
            // Get current mouse position
            POINT mousePos;
            if (GetCursorPos(&mousePos)) {
                // Update hover state
                updateMousePosition(mousePos);
            }

            // Sleep for polling interval
            std::this_thread::sleep_for(
                std::chrono::milliseconds(m_pollingIntervalMs)
            );
        } catch (const std::exception& e) {
            aura::logging::Logger::getInstance().error(
                "hover",
                std::string("Polling thread error: ") + e.what()
            );
        }
    }

    aura::logging::Logger::getInstance().debug("hover", "Polling thread stopped");
}

void HoverDetector::onTaskbarStateChanged(const TaskbarState& newState) {
    std::unique_lock<std::shared_mutex> lock(m_cacheMutex);

    // Update taskbar rect
    m_taskbarRect = newState.taskbarRect;

    // Update icon cache
    m_iconCache = newState.icons;

    aura::logging::Logger::getInstance().debug(
        "hover",
        std::string("Taskbar state updated: ") + std::to_string(m_iconCache.size()) + " icons"
    );
}

uint32_t HoverDetector::hitTestIcons(const POINT& mousePos) const {
    for (uint32_t i = 0; i < m_iconCache.size(); i++) {
        const TaskbarIconInfo& icon = m_iconCache[i];

        if (mousePos.x >= icon.iconRect.left && mousePos.x <= icon.iconRect.right &&
            mousePos.y >= icon.iconRect.top && mousePos.y <= icon.iconRect.bottom) {
            return i;
        }
    }

    return UINT32_MAX;
}

bool HoverDetector::shouldProcessStateChange() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    uint64_t elapsed = nowMs - m_lastStateChangeTimeMs;
    bool shouldProcess = (elapsed >= m_debounceDelayMs);

    if (shouldProcess) {
        m_lastStateChangeTimeMs = nowMs;
    }

    return shouldProcess;
}

}  // namespace aura::taskbar
