# AuraShell — Microsoft Store Listing

> **Status:** Draft — pending code signing. Do NOT submit until a valid EV or
> Trusted Signing certificate is attached. See `code-signing/SIGNING_STRATEGY.md`.

---

## App Identity

| Field             | Value |
|-------------------|-------|
| **App name**      | AuraShell |
| **Publisher**     | iajen |
| **Version**       | 0.1.0-alpha |
| **Category**      | Utilities & Tools |
| **Subcategory**   | Productivity |
| **Privacy URL**   | https://iajensen222222.github.io/AuraShell-Website/privacy-policy |
| **Support URL**   | https://github.com/Iajensen222222/AuraShell/issues |
| **Website URL**   | https://iajensen222222.github.io/AuraShell-Website/ |

---

## Short Description (max 1000 chars — current: ~810)

AuraShell transforms your Windows 11 desktop into a living, breathing visual experience.
Hover over any taskbar icon and watch a real-time audio-reactive glow bloom around it —
driven by your system's live audio spectrum. Assign a unique color theme to each virtual
desktop so your workspace identity changes the moment you switch. The always-on acrylic
backdrop and 60 fps DirectX overlay stay under 1% idle CPU, so you get beauty without
battery drain. A six-page WinUI 3 configuration app lets you tune every parameter live,
from accent color and animation speed to per-shortcut icon replacement. Open-source,
no telemetry, no subscription — AuraShell is a tool for people who care about their
desktop the way developers care about their code.

---

## Long Description (max 10000 chars — current: ~5,480)

### Make Windows 11 Look Like It Was Built for You

AuraShell is an open-source Windows 11 customization suite that adds audio-reactive glow
overlays, per-virtual-desktop themes, DWM acrylic backdrops, and real-time spectrum
visualization to your taskbar — all under 1% idle CPU.

---

### Audio-Reactive Glow Overlays

AuraShell captures your system audio in real time via WASAPI loopback and runs a
128-band FFT spectrum analysis. The result drives a Direct2D overlay that renders
luminous glow rings around every taskbar icon — each bar responding to a different
frequency band. Bass hits pulse the center icons; highs ripple outward. The overlay
runs on a dedicated 60 fps render thread with adaptive frame-rate scaling so it never
interferes with gaming or video.

**Key specs:**
- 128-band FFT at < 50 ms input-to-pixel latency
- EMA smoothing (configurable) prevents visual flicker on transient peaks
- Per-band sensitivity slider in the Visuals page
- Automatic silence detection: overlay dims gracefully when no audio is playing

---

### Hover Animation with Spring Physics

When you mouse over a taskbar icon, AuraShell scales it up 25% using an OutQuad easing
curve with a 150 ms enter / 180 ms exit timing — giving a tactile, physical feel that
still snaps back quickly. The animation is driven by a configurable spring integrator
and runs entirely on the render thread without touching the UI thread.

The acrylic backdrop (DWM `DWMSBT_TRANSIENTWINDOW`) appears simultaneously behind the
glow rings, giving the hovered icon a frosted-glass halo that integrates with Windows 11's
Fluent Design system.

---

### Per-Virtual-Desktop Themes

AuraShell's WorkspaceManager lets you assign a completely different color identity to
each virtual desktop. Switch from Desktop 1 (Cobalt blue, 100% animation speed) to
Desktop 2 (Ember orange, slow pulse) and AuraShell detects the desktop switch via
IVirtualDesktopManager, writes the new accent color to the Windows registry, toggles
dark/light mode if configured, and broadcasts WM_SETTINGCHANGE — all within 500 ms.

All registry writes are HKCU (no elevation required) and automatically backed up.
Uninstall or click "Restore Defaults" to undo every system setting AuraShell has touched.

---

### Real-Time Spectrum Visualizer in the Config App

The WinUI 3 configuration app's Visuals page includes a 128-bar canvas visualizer that
updates at ~10 fps, driven by live audio data streamed from the background service via
the named-pipe IPC protocol. Each bar corresponds to one FFT frequency band and renders
in AuraShell's signature orange-to-white gradient.

---

### Live Configuration — No Restart Required

The six-page WinUI 3 configuration app (Dashboard, Visuals, Behaviors, Desktop Items,
Settings, About) talks to the background service over a secure named pipe using a typed
binary protocol. Every change you make — accent color, animation speed, per-shortcut icon
— is pushed to the service and applied immediately. No restart. No "apply and relaunch."

**Dashboard:** Pick from 12 curated glow presets (AuraShell, Cobalt, Ultraviolet, Sakura,
Ember, Solar, Mint, Sky, Plasma, Coral, Teal, Slate). Each preset ships a matching accent
color and optimized animation speed. Create a desktop shortcut in one click.

**Visuals:** Enter a custom hex color, fine-tune animation speed with a slider, and watch
the live spectrum visualizer react to your music in real time.

**Behaviors:** Toggle auto-start on login (writes the Windows Run key), enable or disable
the taskbar overlay, and view live service uptime and version.

**Desktop Items:** Assign custom icons to individual shortcuts or entire folders using the
IShellLinkW COM interface — no third-party utilities required.

**Settings:** Dark / Light / System theme selector with immediate application. Notification
preference. All settings persist to a JSON file in `%LOCALAPPDATA%\AuraShell\`.

---

### Designed for Performance

AuraShell was engineered from the first line to run invisibly in the background:

- **Idle CPU < 1%** — validated by the built-in PerformanceLogger (1 Hz background sampler)
- **Layered window overlay** — paints on top without touching any application's rendering
- **Lock-free frame recording** — recordFrame() uses a 64-slot atomic ring buffer
- **RAII throughout** — no raw Win32 handle leaks; every kernel object is wrapped
- **spdlog logging** — 10 MB rotating log at `%LOCALAPPDATA%\AuraShell\logs\`

---

### Background Service Architecture

AuraShell separates the configuration UI from the rendering engine:

- **AuraConfig.exe** — WinUI 3 configuration app. Runs as the current user. Talks to the
  service via a DACL-restricted named pipe.
- **AuraShellService.exe** — Lightweight background service registered with the Windows SCM.
  Owns the WASAPI capture, the DirectX overlay, and the named-pipe server.

The installer registers the service and starts it automatically. If the service is not
running, the configuration app falls back gracefully to local-only mode.

---

### Open Source, No Telemetry

AuraShell is MIT-licensed and fully open source at
https://github.com/Iajensen222222/AuraShell.
Zero telemetry. Zero analytics. Zero subscriptions. The app never phones home.

---

### System Requirements

- Windows 11 version 22H2 (build 22621) or later
- x64 processor
- DirectX 11-capable GPU
- 200 MB disk space
- ~150 MB RAM during active use (overlay + service)
- WASAPI loopback audio (Windows 11 default — no drivers required)

---

## Age Rating

**PEGI 3 / Everyone** — No violence, no adult content.

---

## Screenshots

> **Status:** Screenshots not yet captured. Needed before Store submission.

| # | Description | Resolution | File |
|---|-------------|------------|------|
| 1 | Dashboard — preset grid with live service status chip | 1920×1080 | `screenshots/01-dashboard.png` |
| 2 | Visuals — 128-bar audio spectrum visualizer active | 1920×1080 | `screenshots/02-visuals-spectrum.png` |
| 3 | Taskbar hover — acrylic backdrop + glow rings | 1920×1080 | `screenshots/03-taskbar-hover.png` |
| 4 | Behaviors — service uptime + auto-start toggle | 1280×720 | `screenshots/04-behaviors.png` |
| 5 | Desktop Items — custom icon assignment | 1280×720 | `screenshots/05-desktop-items.png` |

**Microsoft Store minimum:** 3 screenshots at ≥ 1366×768. Items 1–3 satisfy this.

**Action required:** Launch AuraConfig.exe with AuraShellService.exe --console running,
navigate to each page, capture screenshots, save to `installer/microsoft-store/screenshots/`.

---

## Promotional Art

| Asset | Size | Status |
|-------|------|--------|
| Store logo (PNG, no transparency) | 300×300 | **NEEDED** |
| Promotional tile (PNG) | 1920×1080 | **NEEDED** |

---

## Pre-Submission Checklist

- [ ] Code signing certificate obtained and binaries signed
- [ ] Build `.msix` package (required; the `.exe` installer is for direct distribution)
- [ ] Privacy policy live at the URL above
- [ ] All 3+ screenshots captured and uploaded
- [ ] Promotional artwork created (300×300 logo minimum)
- [ ] App capabilities declared in `Package.appxmanifest`:
  - `runFullTrust` (required for Win32 process + registry writes)
  - Any background task capabilities used by the service
- [ ] Age rating questionnaire completed in Partner Center
- [ ] Store listing reviewed by a second person for grammar/tone
