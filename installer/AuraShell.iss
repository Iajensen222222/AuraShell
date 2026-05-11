; AuraShell.iss — Inno Setup 6 installer script
; Produces: AuraShell-1.0-Setup.exe
;
; Requirements:
;   Inno Setup 6.x  https://jrsoftware.org/isdl.php
;   Build binaries placed in ..\out\build\x64-Release\bin\
;
; Build:
;   iscc AuraShell.iss
;   (or open in Inno Setup Compiler IDE and press Ctrl+F9)

#define AppName      "AuraShell"
#define AppVersion   "1.0.0"
#define AppPublisher "AuraShell Development Team"
#define AppURL       "https://github.com/Iajensen222222/AuraShell"
#define AppExeName   "AuraConfig.exe"
#define ServiceExe   "AuraShellService.exe"
#define BuildBinDir  "..\out\build\x64-Release\bin"

[Setup]
; Metadata
AppId               = {{A3F2C7D1-8B4E-4A9F-BC12-6E5D0A1F3C8B}
AppName             = {#AppName}
AppVersion          = {#AppVersion}
AppPublisher        = {#AppPublisher}
AppPublisherURL     = {#AppURL}
AppSupportURL       = {#AppURL}
AppUpdatesURL       = {#AppURL}
DefaultDirName      = {autopf}\{#AppName}
DefaultGroupName    = {#AppName}
AllowNoIcons        = no
OutputDir           = ..\dist
OutputBaseFilename  = AuraShell-{#AppVersion}-Setup
SetupIconFile       = ; (set to an .ico path if available)
Compression         = lzma2/ultra64
SolidCompression    = yes
WizardStyle         = modern

; Require Windows 11 (build 22000+) — SDK 22621 features needed.
MinVersion          = 10.0.22000

; Service registration requires elevation.
PrivilegesRequired  = admin
PrivilegesRequiredOverridesAllowed = dialog

; Architecture: 64-bit only.
ArchitecturesInstallIn64BitMode = x64
ArchitecturesAllowed            = x64

; Uninstall
UninstallDisplayName  = {#AppName} {#AppVersion}
UninstallDisplayIcon  = {app}\{#AppExeName}
CreateUninstallRegKey = yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startmenu"; Description: "Create {#AppName} Start Menu shortcuts"; GroupDescription: "Additional icons:"
Name: "autostart";  Description: "Start {#AppName} service automatically with Windows (recommended)"; GroupDescription: "Startup:"; Flags: checked

[Files]
; Main binaries — must be present before running the installer.
Source: "{#BuildBinDir}\{#ServiceExe}"; DestDir: "{app}";           Flags: ignoreversion
Source: "{#BuildBinDir}\{#AppExeName}"; DestDir: "{app}";           Flags: ignoreversion

; Bundled installer scripts (for future repair / manual uninstall).
Source: "install.ps1";                  DestDir: "{app}";           Flags: ignoreversion
Source: "smoke_test.ps1";               DestDir: "{app}";           Flags: ignoreversion

[Dirs]
; Per-machine install dir (created by Inno Setup automatically).

; Per-user AppData dirs — %LOCALAPPDATA%\AuraShell\{logs,themes}
; These are created for the installing user; they are also created at runtime
; on first use for any other user.
Name: "{localappdata}\AuraShell"
Name: "{localappdata}\AuraShell\logs"
Name: "{localappdata}\AuraShell\themes"

[Icons]
; Start Menu shortcuts
Name: "{group}\AuraShell Configuration"; Filename: "{app}\{#AppExeName}";  Comment: "Configure AuraShell visual overlays"; Tasks: startmenu
Name: "{group}\Uninstall {#AppName}";   Filename: "{uninstallexe}";        Comment: "Remove AuraShell from this computer"; Tasks: startmenu

[Run]
; Register and start the service immediately after installation.
Filename: "{app}\{#ServiceExe}"; Parameters: "--install"; \
    Description: "Registering AuraShellService"; \
    StatusMsg:   "Registering Windows service..."; \
    Flags: runhidden waituntilterminated

Filename: "{app}\{#ServiceExe}"; Parameters: "--start"; \
    Description: "Starting AuraShellService"; \
    StatusMsg:   "Starting service..."; \
    Flags: runhidden waituntilterminated

; Offer to launch AuraConfig after install.
Filename: "{app}\{#AppExeName}"; \
    Description: "Launch {#AppName} Configuration"; \
    Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
; Stop and unregister the service before files are removed.
Filename: "{app}\{#ServiceExe}"; Parameters: "--stop";      Flags: runhidden waituntilterminated
Filename: "{app}\{#ServiceExe}"; Parameters: "--uninstall"; Flags: runhidden waituntilterminated

[Code]
// ---------------------------------------------------------------------------
// Custom pre-install check: verify Windows 11 build 22000+
// (Inno Setup's MinVersion handles this, but we add a clearer message.)
// ---------------------------------------------------------------------------
function InitializeSetup(): Boolean;
var
  OSVer: TWindowsVersion;
begin
  GetWindowsVersionEx(OSVer);
  if (OSVer.Major < 10) or
     ((OSVer.Major = 10) and (OSVer.Build < 22000)) then
  begin
    MsgBox(
      'AuraShell requires Windows 11 (build 22000 or later).' + #13#10 +
      'Your system is running an unsupported version of Windows.',
      mbError, MB_OK
    );
    Result := False;
  end else
    Result := True;
end;

// ---------------------------------------------------------------------------
// Pre-uninstall: warn the user and ask for confirmation.
// ---------------------------------------------------------------------------
function InitializeUninstall(): Boolean;
begin
  Result := MsgBox(
    'This will stop and remove AuraShell and AuraShellService.' + #13#10 +
    'Your saved configuration in %LOCALAPPDATA%\AuraShell\ will be preserved.' + #13#10 + #13#10 +
    'Continue with uninstallation?',
    mbConfirmation, MB_YESNO
  ) = IDYES;
end;
