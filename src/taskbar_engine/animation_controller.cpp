#define NOMINMAX       // prevent Windows.h min/max from clobbering std::clamp
#include "animation_controller.h"

#include <algorithm>
#include <cmath>
#include <chrono>

#include <Windows.h>
#include <dwmapi.h>  // DwmFlush — compositor-synchronised sleep

#include "performance_logger.h"

namespace aura::taskbar {

// ============================================================================
// Easing curves — all take t in [0,1] and return a value in [0,1]
// ============================================================================

float applyEasing(float t, EasingType const e) noexcept {
    // Clamp t to [0, 1] so callers don't need to guard.
    t = std::clamp(t, 0.0f, 1.0f);

    switch (e) {
        case EasingType::Linear:
            return t;

        case EasingType::OutQuad:
            // f(t) = 1-(1-t)^2 — fast start, decelerates.
            return 1.0f - (1.0f - t) * (1.0f - t);

        case EasingType::InOutCubic:
            // f(t) = 4t^3           for t < 0.5
            //      = 1-(-2t+2)^3/2  for t >= 0.5
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            } else {
                float const u = -2.0f * t + 2.0f;
                return 1.0f - u * u * u * 0.5f;
            }
    }
    return t;  // unreachable; satisfies MSVC /W4
}

// ============================================================================
// AnimationController — implementation
// ============================================================================

AnimationController::~AnimationController() {
    shutdown();
}

void AnimationController::initialize(uint32_t const maxOverlays, TickCallback cb) {
    if (m_running.exchange(true)) {
        return;  // already initialised
    }

    m_tickCallback = std::move(cb);

    uint32_t const slotCount = std::min(maxOverlays, MAX_SLOTS);
    m_slots.clear();
    m_slots.resize(slotCount);  // zero-initialises each OverlayAnimation

    m_thread = std::thread([this] { animationLoop(); });
}

void AnimationController::shutdown() {
    if (!m_running.exchange(false)) {
        return;  // already stopped
    }

    // Wake the animation thread if it's waiting on the condition variable.
    m_cv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AnimationController::startTransition(
    uint32_t   const overlayIndex,
    float      const targetAlpha,
    uint32_t   const durationMs,
    EasingType const easing
) {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    if (overlayIndex >= m_slots.size()) return;

    auto& slot = m_slots[overlayIndex];

    // Interruptible: start FROM the current rendered alpha, not from the
    // beginning, so there is never a visible jump on mid-animation reversal.
    float const fromAlpha = slot.currentAlpha;

    auto const nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    bool const wasActive = slot.active;

    slot.fromAlpha   = fromAlpha;
    slot.targetAlpha = std::clamp(targetAlpha, 0.0f, 1.0f);
    slot.startUs     = nowUs;
    slot.durationUs  = static_cast<int32_t>(durationMs) * 1000;
    slot.easingType  = easing;
    slot.active      = (fromAlpha != slot.targetAlpha);

    if (slot.active && !wasActive) {
        ++m_activeCount;
    } else if (!slot.active && wasActive) {
        --m_activeCount;
    }

    // Wake the animation thread if it was sleeping.
    if (slot.active) {
        m_cv.notify_one();
    }
}

float AnimationController::getCurrentAlpha(uint32_t const overlayIndex) const {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    if (overlayIndex >= m_slots.size()) return 0.0f;
    return m_slots[overlayIndex].currentAlpha;
}

bool AnimationController::isAnimating(uint32_t const overlayIndex) const {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    if (overlayIndex >= m_slots.size()) return false;
    return m_slots[overlayIndex].active;
}

uint32_t AnimationController::activeCount() const {
    return m_activeCount.load(std::memory_order_relaxed);
}

// ============================================================================
// Animation loop — runs on a dedicated thread
// ============================================================================

void AnimationController::animationLoop() {
    // Stack-allocated pending list — no heap allocation per frame.
    struct PendingTick {
        uint32_t idx;
        float    alpha;
    };
    PendingTick pending[MAX_SLOTS];

    while (m_running.load(std::memory_order_relaxed)) {

        // --- 1. Sleep while idle -------------------------------------------
        // Block until at least one slot becomes active (or shutdown fires).
        {
            std::unique_lock<std::mutex> lk(m_cvMutex);
            m_cv.wait(lk, [this] {
                return !m_running.load(std::memory_order_relaxed)
                    || m_activeCount.load(std::memory_order_relaxed) > 0;
            });
        }
        if (!m_running.load(std::memory_order_relaxed)) break;

        // --- 2. Vsync-aligned sleep ----------------------------------------
        // DwmFlush() blocks until the DWM composites the current frame and
        // signals readiness for the next one — giving hardware-synchronised
        // ticks at the display's refresh rate (60/120/144 Hz) without a
        // busy-wait. Falls back gracefully if DWM is unavailable (FAILED hr).
        //
        // Phase 3.5 review — DCompositionWaitForCompositorClock vs DwmFlush:
        //
        //   DwmFlush (dwmapi.dll, Vista+):
        //     Fires when DWM finishes the current composition pass. In a
        //     multi-monitor setup DWM composes ALL monitors in one pass, so this
        //     ticks at the *slowest* monitor's refresh rate (or the primary,
        //     depending on DWM configuration). Simple and well-tested.
        //
        //   DCompositionWaitForCompositorClock (dcomp.dll, Win8+):
        //     Part of the DirectComposition family. On independent-flip displays
        //     it CAN tick faster than DwmFlush, but in practice fires at the
        //     same composition rate as DwmFlush for layered windows (our
        //     UpdateLayeredWindow path does NOT use independent flip). Adding
        //     it would pull in dcomp.dll, requires linking against dcomp.lib,
        //     and gains nothing for our WS_EX_LAYERED overlay architecture.
        //
        //   Per-monitor pacing:
        //     Neither API gives per-monitor vsync. True per-monitor pacing
        //     requires a DXGI swapchain with DXGI_SWAP_EFFECT_FLIP_DISCARD and
        //     IDXGIOutput::WaitForVBlank — far more complex than our current
        //     UpdateLayeredWindow path warrants.
        //
        //   Verdict: keep DwmFlush. Our delta-time approach (steady_clock
        //   timestamps, not tick count) already makes animations frame-rate-
        //   independent; DwmFlush is a yield mechanism, not a timer source.
        //   Upgrade to DCompositionWaitForCompositorClock if/when the rendering
        //   backend migrates to a proper DirectComposition visual tree.
        HRESULT const hr = DwmFlush();
        if (FAILED(hr)) {
            // DWM unavailable (VM, early boot, DWM disabled) — use a fixed 16ms
            // sleep to approximate 60 Hz without burning the CPU.
            Sleep(16);
        }

        // Record the frame timestamp so PerformanceLogger can compute FPS.
        aura::logging::PerformanceLogger::getInstance().recordFrame();

        // --- 3. Compute updated alphas, hold lock briefly -------------------
        int32_t pendingCount = 0;

        {
            std::lock_guard<std::mutex> lock(m_slotMutex);

            auto const nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
                OverlayAnimation& slot = m_slots[i];
                if (!slot.active) continue;

                float t = static_cast<float>(nowUs - slot.startUs)
                        / static_cast<float>(slot.durationUs);

                if (t >= 1.0f) {
                    t = 1.0f;
                    slot.active = false;
                    --m_activeCount;
                }

                float const easedT = applyEasing(t, slot.easingType);
                slot.currentAlpha  = slot.fromAlpha
                                   + (slot.targetAlpha - slot.fromAlpha) * easedT;

                // Collect for callback invocation outside the lock.
                if (pendingCount < static_cast<int32_t>(MAX_SLOTS)) {
                    pending[pendingCount++] = {i, slot.currentAlpha};
                }
            }
        }

        // --- 4. Fire callbacks outside lock --------------------------------
        // This is where drawOverlay() is called. Keeping it outside the slot
        // mutex prevents startTransition() from being blocked by expensive
        // D2D / UpdateLayeredWindow work.
        for (int32_t i = 0; i < pendingCount; ++i) {
            if (m_tickCallback) {
                m_tickCallback(pending[i].idx, pending[i].alpha);
            }
        }
    }
}

}  // namespace aura::taskbar
