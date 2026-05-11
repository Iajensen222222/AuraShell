#pragma once

#include <cstdint>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

namespace aura::taskbar {

// ============================================================================
// EasingType — selects the interpolation curve applied to t in [0, 1]
// ============================================================================

enum class EasingType : uint8_t {
    Linear,      // f(t) = t
    OutQuad,     // f(t) = 1-(1-t)^2   — fast start, decelerates (default for hover)
    InOutCubic,  // f(t) = cubic S-curve — smooth enter and exit
};

// ============================================================================
// applyEasing — free function so tests can call it directly
// Input t is clamped to [0, 1] before the curve is applied.
// ============================================================================

[[nodiscard]] float applyEasing(float t, EasingType e) noexcept;

// ============================================================================
// OverlayAnimation — per-overlay animation slot (POD, no heap allocation)
// All timing in microseconds to match std::chrono::steady_clock precision.
// Fits comfortably in one cache line.
// ============================================================================

struct OverlayAnimation {
    float    fromAlpha    = 0.0f;    // alpha at the start of this transition
    float    targetAlpha  = 0.0f;    // alpha we're animating toward
    float    currentAlpha = 0.0f;    // last computed alpha (read by startTransition for interrupt)
    int64_t  startUs      = 0;       // std::chrono::steady_clock epoch, microseconds
    int32_t  durationUs   = 200'000; // 200ms default
    EasingType easingType = EasingType::OutQuad;
    bool     active       = false;
    uint8_t  _pad[2]      = {};      // explicit padding to keep layout predictable
};

// ============================================================================
// AnimationController
//
// Runs a single background thread that wakes on DwmFlush() (compositor-
// synchronised) when animations are active, and blocks on a condition
// variable when idle. This gives vsync-aligned updates at zero CPU cost
// when no overlays are transitioning.
//
// Thread safety:
//   startTransition / getCurrentAlpha / isAnimating — safe from any thread.
//   The tick callback fires on the animation thread. The callback must not
//   acquire m_slotMutex.
//
// Per-frame allocation: none. Pending-tick data lives on the stack in
// animationLoop(). The slot vector is reserved at initialize() time.
// ============================================================================

class AnimationController {
public:
    // Invoked on the animation thread for every active slot each frame.
    // overlayIndex: matches the index passed to startTransition().
    // alpha:        current interpolated alpha in [0, 1].
    using TickCallback = std::function<void(uint32_t overlayIndex, float alpha)>;

    AnimationController() = default;
    ~AnimationController();

    AnimationController(AnimationController const&) = delete;
    AnimationController& operator=(AnimationController const&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    // Call once before use. maxOverlays caps the number of simultaneous
    // animated overlays; the slot vector is reserved to avoid runtime alloc.
    // cb is called on the animation thread — must be lightweight and not lock
    // any mutex that startTransition() might hold.
    void initialize(uint32_t maxOverlays, TickCallback cb);

    // Joins the animation thread. Safe to call multiple times.
    void shutdown();

    // ========================================================================
    // Transition control (thread-safe)
    // ========================================================================

    // Start a transition for overlayIndex toward targetAlpha over durationMs.
    // If an animation is already running for this slot it is interrupted and
    // the new transition starts from getCurrentAlpha() — no jump in output.
    // overlayIndex must be < maxOverlays passed to initialize().
    void startTransition(
        uint32_t   overlayIndex,
        float      targetAlpha,
        uint32_t   durationMs = 200,
        EasingType easing     = EasingType::OutQuad
    );

    // ========================================================================
    // Query (thread-safe)
    // ========================================================================

    // Last alpha delivered to the callback (or 0 if never animated).
    [[nodiscard]] float    getCurrentAlpha(uint32_t overlayIndex) const;

    // True while the slot is actively animating.
    [[nodiscard]] bool     isAnimating(uint32_t overlayIndex) const;

    // Count of slots currently animating (for performance monitoring).
    [[nodiscard]] uint32_t activeCount() const;

private:
    void animationLoop();

    // Maximum active slots for stack-allocated pending array in animationLoop.
    static constexpr uint32_t MAX_SLOTS = 64;

    TickCallback                 m_tickCallback;
    std::vector<OverlayAnimation> m_slots;      // pre-reserved, no runtime alloc

    mutable std::mutex           m_slotMutex;   // guards m_slots read/write
    std::condition_variable      m_cv;          // wakes thread when slots go active
    std::mutex                   m_cvMutex;     // gates m_cv.wait()

    std::thread                  m_thread;
    std::atomic<bool>            m_running{false};
    std::atomic<uint32_t>        m_activeCount{0};
};

}  // namespace aura::taskbar
