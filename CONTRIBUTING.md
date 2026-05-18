# Contributing to AuraShell

Thank you for your interest in contributing! AuraShell is a Windows 11 customization suite written in C++20 with WinUI 3. This guide covers the workflow and standards for contributions.

---

## Prerequisites

- Windows 11 (SDK 22621+)
- Visual Studio 2022 with MSVC v143 and C++ desktop workload
- CMake 3.22+
- vcpkg (clone and set `VCPKG_ROOT` environment variable)

## Building

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

Run tests:
```powershell
cd build
ctest --output-on-failure
```

---

## Workflow

1. **Fork** the repository and create a feature branch: `git checkout -b feature/your-feature-name`
2. **Write tests first** (TDD) — every new feature needs a Catch2 test in `tests/unit/`
3. **Implement** the feature following the coding standards below
4. **Verify** all tests pass and the build has zero warnings (`/W4 /WX`)
5. **Open a PR** against `main` — fill out the PR template completely

---

## Coding Standards

These are enforced — PRs that violate them will be asked to revise before merge.
For the full standard with detailed examples, see [CLAUDE.md](CLAUDE.md) in the repository root.

### RAII for all Win32 handles
Every `HWND`, `HANDLE`, `HKEY`, `HICON` must be wrapped in a RAII guard. Never use raw handles without cleanup logic.

### Explicit HRESULT checking
Every Windows API call returning `HRESULT` must be checked with `AURA_HR_CHECK` (fatal) or `AURA_HR_LOG` (non-fatal). No silent failures.

### `std::wstring` for all Windows paths
Never use `std::string` for file paths, registry keys, or window class names. UTF-16 only.

### Naming conventions
- Namespaces: `aura::[feature]`
- Classes: `PascalCase`
- Functions: `camelCase`
- Private members: `m_` prefix
- Constants: `SCREAMING_SNAKE_CASE`

### No magic numbers
All numeric constants must be named at module scope or in a constants header.

### No blocking calls on the render thread
The DirectX render thread must never block on I/O or synchronization primitives.

### No global state without synchronization
Any static/global variable shared between threads must be protected by a mutex or `std::atomic`.

### Performance targets (must not regress)
- Idle CPU: < 1%
- Audio visualizer: 60 FPS at < 50ms latency
- Theme apply time: < 500ms

---

## PR Checklist

Your PR description should confirm:
- [ ] All existing unit tests pass
- [ ] New tests added for the feature/fix
- [ ] No HRESULT goes unchecked
- [ ] All Win32 handles use RAII
- [ ] No `std::string` used for Windows paths
- [ ] No magic numbers introduced
- [ ] Build compiles with zero warnings at `/W4`
- [ ] Idle CPU impact verified (< 1% in idle state)

---

## What We're Looking For

Good first contributions:
- Adding missing unit tests (see `tests/unit/`)
- Improving error messages and log output
- Documentation and code comments (only where WHY is non-obvious)
- Performance profiling and optimization

Larger contributions should open a GitHub Discussion first to align on design before investing implementation time.

---

## Code of Conduct

Be respectful. Focus feedback on the code, not the person. We're all here to build something good.
