#Requires -Version 5.1
<#
.SYNOPSIS
    AuraShell 1.0 Installer

.DESCRIPTION
    Installs AuraShell binaries to C:\Program Files\AuraShell, registers
    AuraShellService with the Windows SCM (auto-start), creates required
    AppData directories, and adds AuraConfig to the Start Menu.

    Must be run as Administrator.

.PARAMETER SourceDir
    Directory containing the built binaries (AuraShellService.exe,
    AuraConfig.exe).  Defaults to the build output directory relative to
    the repository root.

.PARAMETER InstallDir
    Destination directory.  Defaults to 'C:\Program Files\AuraShell'.

.PARAMETER Uninstall
    If specified, stops and removes AuraShell instead of installing it.

.EXAMPLE
    # Install from default build output:
    .\install.ps1

    # Install from a custom build:
    .\install.ps1 -SourceDir "D:\builds\AuraShell\bin"

    # Uninstall:
    .\install.ps1 -Uninstall
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $SourceDir  = "",
    [string] $InstallDir = "C:\Program Files\AuraShell",
    [switch] $Uninstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ============================================================================
# Elevation guard
# ============================================================================

function Assert-Admin {
    $identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error "This script must be run as Administrator.  Re-launch PowerShell with 'Run as Administrator'."
        exit 1
    }
}

Assert-Admin

# ============================================================================
# Helpers
# ============================================================================

function Write-Step([string]$msg) {
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Write-OK([string]$msg) {
    Write-Host "    [OK] $msg" -ForegroundColor Green
}

function Write-Fail([string]$msg) {
    Write-Host "    [FAIL] $msg" -ForegroundColor Red
}

$ServiceName    = "AuraShellService"
$ServiceExe     = "AuraShellService.exe"
$ConfigExe      = "AuraConfig.exe"
$StartMenuDir   = [Environment]::GetFolderPath("CommonPrograms")
$StartMenuGroup = "$StartMenuDir\AuraShell"

# Resolve SourceDir — default to the CMake build output beside the repo root.
if (-not $SourceDir) {
    $scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $repoRoot   = Split-Path -Parent $scriptRoot
    $SourceDir  = Join-Path $repoRoot "out\build\x64-Release\bin"
    if (-not (Test-Path $SourceDir)) {
        # Fall back to Debug build if Release not present.
        $SourceDir = Join-Path $repoRoot "out\build\x64-Debug\bin"
    }
}

# ============================================================================
# UNINSTALL
# ============================================================================

if ($Uninstall) {
    Write-Step "Stopping AuraShellService"
    $svcExePath = Join-Path $InstallDir $ServiceExe
    if (Test-Path $svcExePath) {
        & $svcExePath --stop 2>$null
    } else {
        Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    }
    Write-OK "Service stopped"

    Write-Step "Unregistering AuraShellService"
    if (Test-Path $svcExePath) {
        & $svcExePath --uninstall
    } else {
        sc.exe delete $ServiceName 2>$null | Out-Null
    }
    Write-OK "Service unregistered"

    Write-Step "Removing Start Menu shortcuts"
    if (Test-Path $StartMenuGroup) {
        Remove-Item -Recurse -Force $StartMenuGroup
        Write-OK "Start Menu group removed"
    } else {
        Write-OK "Start Menu group not found — skipping"
    }

    Write-Step "Removing install directory"
    if (Test-Path $InstallDir) {
        Remove-Item -Recurse -Force $InstallDir
        Write-OK "$InstallDir removed"
    } else {
        Write-OK "$InstallDir not found — skipping"
    }

    Write-Host "`nAuraShell has been uninstalled." -ForegroundColor Yellow
    exit 0
}

# ============================================================================
# INSTALL
# ============================================================================

# ---- 1. Validate source binaries -------------------------------------------

Write-Step "Validating source binaries in: $SourceDir"

$requiredFiles = @($ServiceExe, $ConfigExe)
foreach ($f in $requiredFiles) {
    $path = Join-Path $SourceDir $f
    if (-not (Test-Path $path)) {
        Write-Fail "$f not found at $path"
        Write-Host "  Build AuraShell first: cmake --build . --config Release --parallel 4" -ForegroundColor Yellow
        exit 1
    }
}
Write-OK "All required binaries found"

# ---- 2. Create install directory and copy binaries -------------------------

Write-Step "Installing binaries to $InstallDir"

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Force $InstallDir | Out-Null
}

foreach ($f in $requiredFiles) {
    $src  = Join-Path $SourceDir $f
    $dest = Join-Path $InstallDir $f
    Copy-Item -Force $src $dest
    Write-OK "Copied $f"
}

# ---- 3. Create AppData directories for logs and config ---------------------

Write-Step "Creating per-user AppData directories"

# These directories are created in the current user's LOCALAPPDATA.
# On a multi-user machine the service creates them on first run per user.
$localAppData  = [Environment]::GetFolderPath("LocalApplicationData")
$auraDataRoot  = Join-Path $localAppData "AuraShell"
$auraDirs      = @(
    $auraDataRoot,
    (Join-Path $auraDataRoot "logs"),
    (Join-Path $auraDataRoot "themes")
)
foreach ($dir in $auraDirs) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force $dir | Out-Null
    }
    Write-OK $dir
}

# ---- 4. Register AuraShellService with the SCM -----------------------------

Write-Step "Registering AuraShellService with the Windows SCM"

$svcExePath = Join-Path $InstallDir $ServiceExe

# Check if already registered — unregister first to pick up new binary path.
$existingSvc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($existingSvc) {
    Write-Host "    Service already registered — updating registration..." -ForegroundColor Yellow
    & $svcExePath --stop    | Out-Null
    & $svcExePath --uninstall | Out-Null
}

$result = & $svcExePath --install
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Service registration failed (exit $LASTEXITCODE)"
    Write-Host $result
    exit 1
}
Write-OK "Service registered (AUTO_START, LocalSystem, failure recovery configured)"

# ---- 5. Start the service --------------------------------------------------

Write-Step "Starting AuraShellService"

$result = & $svcExePath --start
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Service failed to start (exit $LASTEXITCODE)"
    Write-Host "  Check Event Viewer > Windows Logs > Application for details." -ForegroundColor Yellow
    # Non-fatal: installation is complete; the service will start at next boot.
} else {
    Write-OK "AuraShellService is running"
}

# ---- 6. Add to Start Menu --------------------------------------------------

Write-Step "Creating Start Menu shortcuts"

if (-not (Test-Path $StartMenuGroup)) {
    New-Item -ItemType Directory -Force $StartMenuGroup | Out-Null
}

$wshShell = New-Object -ComObject WScript.Shell

# AuraConfig shortcut
$lnkConfig = $wshShell.CreateShortcut("$StartMenuGroup\AuraShell Configuration.lnk")
$lnkConfig.TargetPath       = Join-Path $InstallDir $ConfigExe
$lnkConfig.WorkingDirectory = $InstallDir
$lnkConfig.Description      = "Configure AuraShell visual overlays"
$lnkConfig.Save()
Write-OK "AuraShell Configuration.lnk"

# Uninstall shortcut
$uninstallScript = Join-Path $InstallDir "uninstall.ps1"
Copy-Item -Force $MyInvocation.MyCommand.Path $uninstallScript
$lnkUninstall = $wshShell.CreateShortcut("$StartMenuGroup\Uninstall AuraShell.lnk")
$lnkUninstall.TargetPath       = "powershell.exe"
$lnkUninstall.Arguments        = "-ExecutionPolicy Bypass -File `"$uninstallScript`" -Uninstall"
$lnkUninstall.WorkingDirectory = $InstallDir
$lnkUninstall.Description      = "Remove AuraShell"
$lnkUninstall.Save()
Write-OK "Uninstall AuraShell.lnk"

# ---- 7. Add to system PATH (optional, allows CLI usage) --------------------

Write-Step "Adding $InstallDir to system PATH"

$sysPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($sysPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$sysPath;$InstallDir", "Machine")
    Write-OK "Added to system PATH (takes effect in new shells)"
} else {
    Write-OK "Already in system PATH"
}

# ============================================================================
# Done
# ============================================================================

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host " AuraShell 1.0 installed successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Install directory : $InstallDir"
Write-Host "  Service name      : $ServiceName  (AUTO_START)"
Write-Host "  Config UI         : Start Menu > AuraShell > AuraShell Configuration"
Write-Host "  Logs              : $auraDataRoot\logs\"
Write-Host "  Config file       : $auraDataRoot\config.json  (created on first save)"
Write-Host ""
Write-Host "Run smoke_test.ps1 to verify the installation." -ForegroundColor Cyan
