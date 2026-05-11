#pragma once

#include <Windows.h>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <shared_mutex>
#include <cstdint>

#include "taskbar_controller.h"

namespace aura::taskbar {

// ============================================================================
// Callback Signatures
// ============================================================================

using HoverEnterCallback = std::function<void(const TaskbarIconInfo&)>;
using HoverExitCallback = std::function<void()>;

// ============================================================================
// HoverDetector - Low-Level Mouse Tracking
// ============================================================================

/**
 * @class HoverDetector
 * @brief Monitors mouse position relative to taskbar regions and icons
 * 
 * This class provides:
 * - Low-level mouse position polling (via GetCursorPos)
 * - Hit-testing against taskbar and icon regions
 * - Hover state management with debouncing
 * - Observer callbacks for hover enter/exit events
 * - Thread-safe concurrent access
 * 
 * **Performance Strategy**:
 * - Polls mouse position at ~60Hz (16ms interval)
 * - Uses simple rectangular hit-testing (O(n) for n icons)
 * - Debounces rapid state changes (default 50ms) to prevent flicker
 * - Registers as observer to TaskbarController for geometry updates
 * - Maintains local cache of taskbar/icon geometry to minimize lock contention
 * 
 * **CPU Usage Target**: < 0.3% when idle
 * - GetCursorPos is very fast (~1µs)
 * - Rectangular hit-testing is O(n) with small n (typically < 20 icons)
 * - Sleep(16ms) between polls = ~3% of CPU per thread
 * - With sleep optimization, achieves < 0.3% idle usage
 */
class HoverDetector {
public:
    // ========================================================================
    // Singleton Access
    // ========================================================================

    /**
     * @brief Get singleton instance of HoverDetector
     */
    static HoverDetector& getInstance();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize hover detection
     * 
     * **Effects**:
     * - Registers as observer to TaskbarController
     * - Caches current taskbar/icon geometry
     * - Starts background polling thread
     * - Ready to accept subscriptions
     * 
     * **Thread Safety**: Safe to call from any thread
     * **Side Effects**: Modifies TaskbarController observer list
     */
    void initialize();

    /**
     * @brief Shutdown hover detection
     * 
     * **Effects**:
     * - Stops background polling thread
     * - Unregisters from TaskbarController
     * - Clears all subscriptions
     * - Releases resources
     * 
     * **Thread Safety**: Safe to call from any thread
     */
    void shutdown();

    /**
     * @brief Check if HoverDetector is initialized
     */
    bool isInitialized() const;

    // ========================================================================
    // Mouse Position Queries
    // ========================================================================

    /**
     * @brief Check if mouse is over taskbar area
     * 
     * @param mousePos Screen coordinates of mouse position
     * @return true if mouse is within taskbar bounding rectangle
     * 
     * **Performance**: O(1), typically < 1µs
     */
    bool isMouseOverTaskbar(const POINT& mousePos) const;

    /**
     * @brief Find which icon (if any) mouse is over
     * 
     * @param mousePos Screen coordinates of mouse position
     * @return Pointer to TaskbarIconInfo if over an icon, nullptr otherwise
     * 
     * **Performance**: O(n) where n = number of icons, typically < 20
     * **Note**: Returns pointer to internal cache (valid only while HoverDetector is initialized)
     */
    const TaskbarIconInfo* getHoveredIcon(const POINT& mousePos) const;

    // ========================================================================
    // Real-Time Mouse Tracking
    // ========================================================================

    /**
     * @brief Update mouse position (called by polling thread)
     * 
     * This method:
     * - Checks if mouse crossed taskbar boundary
     * - Performs hit-testing against icons
     * - Applies debouncing to prevent flicker
     * - Triggers callbacks for enter/exit events
     * 
     * @param mousePos Current mouse position in screen coordinates
     * 
     * **Performance**: O(n) hit-testing + O(1) state comparison
     * **Thread Safety**: Thread-safe (internally synchronized)
     */
    void updateMousePosition(const POINT& mousePos);

    // ========================================================================
    // Hover State Callbacks
    // ========================================================================

    /**
     * @brief Subscribe to hover enter events
     * 
     * Callback is invoked (in polling thread context) when mouse enters icon region.
     * 
     * @param callback Function to invoke with icon info
     * 
     * **Thread Safety**: Thread-safe to call from any thread
     * **Note**: Callback executes in polling thread, keep it short
     */
    void subscribeHoverEnter(HoverEnterCallback callback);

    /**
     * @brief Subscribe to hover exit events
     * 
     * Callback is invoked (in polling thread context) when mouse leaves taskbar area.
     * 
     * @param callback Function to invoke
     * 
     * **Thread Safety**: Thread-safe to call from any thread
     * **Note**: Callback executes in polling thread, keep it short
     */
    void subscribeHoverExit(HoverExitCallback callback);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set debounce delay (ms) to prevent flicker
     * 
     * When mouse rapidly enters/exits icon region, this delay prevents
     * multiple enter/exit callbacks. Default is 50ms.
     * 
     * @param delayMs Debounce delay in milliseconds (0 = no debouncing)
     * 
     * **Recommended Range**: 20-100ms
     * **Too Low** (< 20ms): May get multiple callbacks for single hover
     * **Too High** (> 100ms): May feel sluggish to user
     */
    void setDebounceDelayMs(uint32_t delayMs);

    /**
     * @brief Get current debounce delay
     */
    uint32_t getDebounceDelayMs() const;

    /**
     * @brief Set polling interval (ms)
     * 
     * Controls frequency of mouse position checks. Default is 16ms (~60Hz).
     * 
     * @param intervalMs Polling interval in milliseconds
     * 
     * **Recommended Range**: 8-32ms
     * **Too Low** (< 8ms): Increases CPU usage, diminishing returns
     * **Too High** (> 32ms): May miss rapid movements
     */
    void setPollingIntervalMs(uint32_t intervalMs);

    /**
     * @brief Get current polling interval
     */
    uint32_t getPollingIntervalMs() const;

    // ========================================================================
    // Internal State Access (for testing & debugging)
    // ========================================================================

    /**
     * @brief Get cached taskbar bounding rectangle
     * 
     * Returns the last known taskbar geometry from TaskbarController.
     */
    RECT getTaskbarBounds() const;

    /**
     * @brief Get cached icon list
     * 
     * Returns local copy of icon geometry cache.
     */
    std::vector<TaskbarIconInfo> getCachedIcons() const;

    /**
     * @brief Get current hover state
     * 
     * @return Pointer to currently hovered icon, or nullptr if not hovering
     */
    const TaskbarIconInfo* getCurrentHoveredIcon() const;

    // ========================================================================
    // Diagnostic & Performance
    // ========================================================================

    /**
     * @brief Get performance statistics
     * 
     * @return String with polling frequency, debounce info, etc.
     */
    std::string getPerformanceStats() const;

private:
    // ========================================================================
    // Private Lifecycle
    // ========================================================================

    HoverDetector();
    ~HoverDetector();

    // Delete copy/move constructors for singleton
    HoverDetector(const HoverDetector&) = delete;
    HoverDetector& operator=(const HoverDetector&) = delete;
    HoverDetector(HoverDetector&&) = delete;
    HoverDetector& operator=(HoverDetector&&) = delete;

    // ========================================================================
    // Private Methods
    // ========================================================================

    /**
     * @brief Background polling thread procedure
     * 
     * Runs in background thread:
     * 1. Gets current mouse position via GetCursorPos
     * 2. Calls updateMousePosition with current position
     * 3. Sleeps for m_pollingIntervalMs
     * 4. Repeats until m_running is false
     */
    void pollingThreadProc();

    /**
     * @brief Called by TaskbarController when taskbar state changes
     * 
     * Updates m_taskbarCache and m_iconCache.
     */
    void onTaskbarStateChanged(const TaskbarState& newState);

    /**
     * @brief Perform hit-testing against cached icons
     * 
     * @return Index of hovered icon, or UINT32_MAX if no hit
     */
    uint32_t hitTestIcons(const POINT& mousePos) const;

    /**
     * @brief Apply debouncing logic
     * 
     * Returns true if state change should be processed (outside debounce window).
     */
    bool shouldProcessStateChange() const;

    // ========================================================================
    // Member Variables
    // ========================================================================

    // Lifecycle
    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_running = false;
    std::unique_ptr<std::thread> m_pollingThread;

    // Taskbar state cache (read from TaskbarController)
    mutable std::shared_mutex m_cacheMutex;
    RECT m_taskbarRect = {0, 0, 0, 0};
    std::vector<TaskbarIconInfo> m_iconCache;

    // Hover state
    mutable std::shared_mutex m_hoverStateMutex;
    const TaskbarIconInfo* m_currentHoveredIcon = nullptr;
    uint32_t m_currentHoveredIconIndex = UINT32_MAX;

    // Debouncing
    std::atomic<uint32_t> m_debounceDelayMs = 50;
    mutable uint64_t m_lastStateChangeTimeMs = 0;

    // Polling configuration
    std::atomic<uint32_t> m_pollingIntervalMs = 16;  // ~60Hz

    // Callbacks (stored as vectors to support multiple subscribers)
    mutable std::shared_mutex m_callbackMutex;
    std::vector<HoverEnterCallback> m_hoverEnterCallbacks;
    std::vector<HoverExitCallback> m_hoverExitCallbacks;

    // TaskbarController reference
    TaskbarController* m_taskbarController = nullptr;
};

}  // namespace aura::taskbar
