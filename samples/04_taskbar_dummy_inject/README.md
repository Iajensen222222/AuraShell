# Sample 04: DLL Injection — Dummy Target (notepad.exe)

**Sprint 3 exit criterion:** Prove that DLL injection into a safe dummy target works before
attempting any taskbar-related injection.

## What this does

`dummy_inject.exe` finds a running `notepad.exe` process and injects `test_hook_dll.dll` into
it using the standard `LoadLibraryW` remote thread technique. The DLL writes a single log entry
to `%LOCALAPPDATA%\AuraShell\inject_test.log` on attach and another on detach — nothing else.

No system hooks. No registry writes. No explorer.exe involvement.

## Safety constraints (from CLAUDE.md)

| Rule | Status |
|---|---|
| **Never inject into explorer.exe** | ENFORCED — target is hardcoded to `notepad.exe` |
| **No global hooks** | ENFORCED — DLL contains only DllMain with file logging |
| **No registry modifications** | ENFORCED — only creates a log file in AppData |
| **Dummy-first testing** | ENFORCED — use notepad.exe; explorer.exe only in a VM later |

## How to run

1. **Open Notepad:** `notepad.exe` must be running before you launch the injector.
2. **Build the sample** (in a Developer Command Prompt):
   ```
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ```
3. **Run as Administrator** (injection requires elevated privileges):
   ```
   .\build\bin\samples\dummy_inject.exe
   ```
4. **Check the log:**
   ```
   Get-Content "$env:LOCALAPPDATA\AuraShell\inject_test.log"
   ```
   Expected output:
   ```
   [1716145200000] test_hook_dll ATTACHED in PID 12345
   ```
5. **Close Notepad** to trigger the DETACH log entry.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `OpenProcess failed: 5` (Access denied) | Run dummy_inject.exe as Administrator |
| `CreateRemoteThread failed: 5` | Same — needs elevation |
| `LoadLibraryW returned handle: 0x00000000` | DLL path is wrong; pass the full path as arg: `dummy_inject.exe "C:\path\to\test_hook_dll.dll"` |
| Windows Defender quarantines the DLL | Temporarily exclude `%LOCALAPPDATA%\AuraShell` from real-time protection |

## What comes next

After this test passes, the next step is to build a real taskbar hook DLL that:
1. Intercepts icon drawing via `SetWindowsHookEx(WH_CBT, ...)` or UI Automation
2. Applies AuraShell visual effects
3. Is tested in an isolated VM against explorer.exe (per CLAUDE.md constraints)

That work is **not in this sample** — this sample only proves the injection mechanism works safely.
