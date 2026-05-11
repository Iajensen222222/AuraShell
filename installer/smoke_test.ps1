#Requires -Version 5.1
<#
.SYNOPSIS
    AuraShell 1.0 Post-Install Smoke Test

.DESCRIPTION
    Verifies that AuraShell was installed correctly on a clean machine.
    Checks binaries, service state, AppData directories, Start Menu
    shortcuts, and basic IPC connectivity.

    Does NOT require Administrator rights to read service status.

.EXAMPLE
    .\smoke_test.ps1
    .\smoke_test.ps1 -InstallDir "D:\AuraShell"
#>

param(
    [string] $InstallDir = "C:\Program Files\AuraShell"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"   # continue on failure so all checks run

$Passed  = 0
$Failed  = 0
$Skipped = 0

# ============================================================================
# Check helpers
# ============================================================================

function Pass([string]$desc) {
    Write-Host "  [PASS] $desc" -ForegroundColor Green
    $script:Passed++
}

function Fail([string]$desc, [string]$detail = "") {
    Write-Host "  [FAIL] $desc" -ForegroundColor Red
    if ($detail) { Write-Host "         $detail" -ForegroundColor DarkRed }
    $script:Failed++
}

function Skip([string]$desc, [string]$reason) {
    Write-Host "  [SKIP] $desc — $reason" -ForegroundColor Yellow
    $script:Skipped++
}

function Section([string]$title) {
    Write-Host "`n--- $title ---" -ForegroundColor Cyan
}

# ============================================================================
# 1. Binaries present
# ============================================================================

Section "Binaries"

$binaries = @(
    "AuraShellService.exe",
    "AuraConfig.exe"
)

foreach ($bin in $binaries) {
    $path = Join-Path $InstallDir $bin
    if (Test-Path $path) {
        $ver = (Get-Item $path).VersionInfo.ProductVersion
        Pass "$bin present$(if ($ver) { " (v$ver)" })"
    } else {
        Fail "$bin NOT found" "Expected: $path"
    }
}

# ============================================================================
# 2. AuraShellService registration
# ============================================================================

Section "Service Registration"

$svc = Get-Service -Name "AuraShellService" -ErrorAction SilentlyContinue
if ($svc) {
    Pass "Service 'AuraShellService' is registered"

    if ($svc.StartType -eq "Automatic") {
        Pass "StartType = Automatic"
    } else {
        Fail "StartType = $($svc.StartType)" "Expected: Automatic"
    }
} else {
    Fail "Service 'AuraShellService' is NOT registered"
}

# ============================================================================
# 3. Service running
# ============================================================================

Section "Service State"

if ($svc) {
    if ($svc.Status -eq "Running") {
        Pass "Service status = Running"
    } else {
        Fail "Service status = $($svc.Status)" "Expected: Running"
    }

    # Check that the service process is alive and using < 2 MB working set.
    $proc = Get-Process -Name "AuraShellService" -ErrorAction SilentlyContinue
    if ($proc) {
        $memMB = [Math]::Round($proc.WorkingSet64 / 1MB, 1)
        if ($memMB -le 2.0) {
            Pass "Service memory = $memMB MB  (target <= 2 MB)"
        } else {
            Fail "Service memory = $memMB MB" "Exceeds 2 MB target — check for leaks"
        }
    } else {
        Skip "Service process memory" "Process not found (may be running as SYSTEM)"
    }
} else {
    Skip "Service state"  "Service not registered"
    Skip "Service memory" "Service not registered"
}

# ============================================================================
# 4. AppData directories
# ============================================================================

Section "AppData Directories"

$localAppData = [Environment]::GetFolderPath("LocalApplicationData")
$requiredDirs = @(
    "$localAppData\AuraShell",
    "$localAppData\AuraShell\logs",
    "$localAppData\AuraShell\themes"
)

foreach ($dir in $requiredDirs) {
    if (Test-Path $dir) {
        Pass $dir
    } else {
        Fail $dir "Directory not created by installer"
    }
}

# ============================================================================
# 5. Named pipe reachability (IPC smoke test)
# ============================================================================

Section "Named Pipe IPC"

$pipeName = "\\.\pipe\AuraShell_Control"

# Test-Path works on named pipes on Windows PowerShell 5.1+
if (Test-Path $pipeName -ErrorAction SilentlyContinue) {
    Pass "Named pipe '$pipeName' is reachable"
} else {
    if ($svc -and $svc.Status -eq "Running") {
        Fail "Named pipe '$pipeName' NOT reachable" "Service is running but pipe is absent — check DACL"
    } else {
        Skip "Named pipe reachability" "Service is not running"
    }
}

# ============================================================================
# 6. Start Menu shortcuts
# ============================================================================

Section "Start Menu"

$startMenuGroup = Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "AuraShell"
$shortcuts = @(
    "AuraShell Configuration.lnk",
    "Uninstall AuraShell.lnk"
)

if (Test-Path $startMenuGroup) {
    Pass "Start Menu group '$startMenuGroup' exists"
    foreach ($lnk in $shortcuts) {
        $lnkPath = Join-Path $startMenuGroup $lnk
        if (Test-Path $lnkPath) {
            Pass "Shortcut: $lnk"
        } else {
            Fail "Shortcut missing: $lnk"
        }
    }
} else {
    Fail "Start Menu group NOT found" "Expected: $startMenuGroup"
}

# ============================================================================
# 7. System PATH
# ============================================================================

Section "System PATH"

$sysPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($sysPath -like "*$InstallDir*") {
    Pass "$InstallDir is in system PATH"
} else {
    Skip "PATH entry" "Not in system PATH — CLI invocation requires full path"
}

# ============================================================================
# 8. Config app launch (non-interactive, exits after 3s)
# ============================================================================

Section "Config App Launch"

$configExe = Join-Path $InstallDir "AuraConfig.exe"
if (Test-Path $configExe) {
    try {
        $proc = Start-Process -FilePath $configExe -PassThru -ErrorAction Stop
        Start-Sleep -Seconds 3

        if (-not $proc.HasExited) {
            Pass "AuraConfig.exe launched and is running (PID $($proc.Id))"
            # Gracefully close the window.
            $proc.CloseMainWindow() | Out-Null
            $proc.WaitForExit(2000) | Out-Null
            if (-not $proc.HasExited) { $proc.Kill() }
        } else {
            Fail "AuraConfig.exe exited immediately (ExitCode $($proc.ExitCode))"
        }
    } catch {
        Fail "AuraConfig.exe failed to launch" $_.Exception.Message
    }
} else {
    Skip "Config app launch" "AuraConfig.exe not found at $configExe"
}

# ============================================================================
# Summary
# ============================================================================

$total = $Passed + $Failed + $Skipped

Write-Host ""
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host " Smoke Test Results: $Passed/$total passed" -ForegroundColor $(if ($Failed -eq 0) {"Green"} else {"Yellow"})
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "  Passed  : $Passed" -ForegroundColor Green
Write-Host "  Failed  : $Failed" $(if ($Failed -gt 0) {"-ForegroundColor Red"})
Write-Host "  Skipped : $Skipped" -ForegroundColor Yellow
Write-Host ""

if ($Failed -gt 0) {
    Write-Host "Installation has issues. Review failures above." -ForegroundColor Red
    Write-Host "Logs: $localAppData\AuraShell\logs\" -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "AuraShell is installed and operational." -ForegroundColor Green
    exit 0
}
