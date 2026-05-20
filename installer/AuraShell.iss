; AuraShell.iss — InnoSetup 6 installer script
; Targets: AuraConfig.exe (UI) + AuraShellService.exe (background service)
; Build output assumed at: <repo>\out\build\x64-Release\bin\

#define AppName        "AuraShell"
#define AppVersion     "0.1.0-alpha"
#define AppPublisher   "iajen"
#define AppURL         "https://iajensen222222.github.io/AuraShell-Website/"
#define AppExeName     "AuraConfig.exe"
#define ServiceExeName "AuraShellService.exe"
#define BuildBinDir    "..\out\build\x64-Release\bin\Release"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
LicenseFile=
OutputDir=Output
OutputBaseFilename=AuraShell-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible

; Show a nice header image if one is available alongside the .iss
; WizardImageFile=WizardImage.bmp
; WizardSmallImageFile=WizardSmallImage.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon";    Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startupicon";   Description: "Start AuraShell automatically with Windows"; GroupDescription: "Startup"; Flags: unchecked

[Files]
; Main UI application
Source: "{#BuildBinDir}\{#AppExeName}";    DestDir: "{app}"; Flags: ignoreversion
; Background service
Source: "{#BuildBinDir}\{#ServiceExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Visual C++ 2022 Redistributable (optional — include in Output\ to bundle)
; Source: "vcredist_x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
; Start menu
Name: "{group}\{#AppName}";              Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}";    Filename: "{uninstallexe}"
; Desktop icon (only when user selected the task)
Name: "{autodesktop}\{#AppName}";        Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Persist install path for self-updater and diagnostics
Root: HKLM; Subkey: "SOFTWARE\{#AppName}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
; Auto-start (written only if user selected the startup task)
Root: HKCU; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; Flags: uninsdeletevalue; Tasks: startupicon
; Cleanup on uninstall
Root: HKCU; Subkey: "SOFTWARE\{#AppName}"; Flags: dontcreatekey uninsdeletekey

[Run]
; Install the service into the SCM
Filename: "{app}\{#ServiceExeName}"; Parameters: "--install"; StatusMsg: "Installing AuraShell service..."; Flags: runhidden waituntilterminated
; Start the service immediately after install
Filename: "{app}\{#ServiceExeName}"; Parameters: "--start";   StatusMsg: "Starting AuraShell service..."; Flags: runhidden waituntilterminated
; Launch the UI for the user at the end of setup
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

; Optional: install VC++ redistributable if needed
; Filename: "{tmp}\vcredist_x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installing Visual C++ 2022 Redistributables..."; Flags: runhidden waituntilterminated; Check: VCRedistNeeded

[UninstallRun]
; Stop the service first, then uninstall it from the SCM
Filename: "{app}\{#ServiceExeName}"; Parameters: "--stop";      Flags: runhidden waituntilterminated; RunOnceId: "StopService"
Filename: "{app}\{#ServiceExeName}"; Parameters: "--uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "UninstallService"

[UninstallDelete]
; Remove the settings folder written by the app
Type: filesandordirs; Name: "{localappdata}\AuraShell"

[Code]
// ── VC++ 2022 Redistributable check ──────────────────────────────────────────
// Checks for the VS 2022 x64 runtime by querying the registry.
// Returns true when the runtime is NOT installed (i.e. we need to install it).
function VCRedistNeeded: Boolean;
var
  version: string;
begin
  Result := not RegQueryStringValue(
    HKLM,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Version',
    version
  );
end;

// ── Custom uninstall confirmation ─────────────────────────────────────────────
function InitializeUninstall: Boolean;
begin
  Result := MsgBox(
    'This will stop and remove the AuraShell service and delete all program files.' +
    #13#10 + 'Your settings in %LOCALAPPDATA%\AuraShell will also be removed.' +
    #13#10#13#10 + 'Continue with uninstall?',
    mbConfirmation, MB_YESNO
  ) = IDYES;
end;
