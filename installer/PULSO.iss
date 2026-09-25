#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef SourceRoot
  #define SourceRoot ".."
#endif
#ifndef OutputRoot
  #define OutputRoot "..\dist"
#endif

#define ProductName "PULSO"
#define Publisher "PULSO Music"
#define VstSource SourceRoot + "\build\windows-release\Pulso_artefacts\Release\VST3\PULSO.vst3"
#define StandaloneSource SourceRoot + "\build\windows-release\Pulso_artefacts\Release\Standalone\PULSO.exe"

[Setup]
AppId={{58C16D30-17B4-4E8A-BEBE-5C57F94A31D8}
AppName={#ProductName}
AppVersion={#AppVersion}
AppVerName={#ProductName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL=https://pulso.music
AppSupportURL=https://pulso.music/support
AppUpdatesURL=https://pulso.music/account
DefaultDirName={autopf}\PULSO
DefaultGroupName=PULSO
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputRoot}
OutputBaseFilename=PULSO-{#AppVersion}-windows-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\PULSO.exe
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\LICENSE.md

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "abletonbridge"; Description: "Instalar la superficie de control PulsoDeployRemote para Ableton Live 12"; Flags: checkedonce

[Files]
Source: "{#VstSource}\*"; DestDir: "{commoncf64}\VST3\PULSO.vst3"; Excludes: "*.incomplete.*"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace
Source: "{#StandaloneSource}"; DestDir: "{app}"; DestName: "PULSO.exe"; Flags: ignoreversion restartreplace
Source: "{#SourceRoot}\ableton\PulsoDeployRemote\*.py"; DestDir: "{commonappdata}\Ableton\Live 12 Suite\Resources\MIDI Remote Scripts\PulsoDeployRemote"; Flags: ignoreversion; Tasks: abletonbridge; Check: AbletonSuiteRemoteRootExists
Source: "{#SourceRoot}\ableton\PulsoDeployRemote\*.py"; DestDir: "{commonappdata}\Ableton\Live 12 Standard\Resources\MIDI Remote Scripts\PulsoDeployRemote"; Flags: ignoreversion; Tasks: abletonbridge; Check: AbletonStandardRemoteRootExists
Source: "{#SourceRoot}\ableton\PulsoDeployRemote\*.py"; DestDir: "{commonappdata}\Ableton\Live 12 Intro\Resources\MIDI Remote Scripts\PulsoDeployRemote"; Flags: ignoreversion; Tasks: abletonbridge; Check: AbletonIntroRemoteRootExists
Source: "{#SourceRoot}\ableton\PulsoDeployRemote\*.py"; DestDir: "{commonappdata}\Ableton\Live 12 Lite\Resources\MIDI Remote Scripts\PulsoDeployRemote"; Flags: ignoreversion; Tasks: abletonbridge; Check: AbletonLiteRemoteRootExists
Source: "{#SourceRoot}\README.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "{#SourceRoot}\docs\*.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "{#SourceRoot}\scripts\support-bundle.ps1"; DestDir: "{app}\Support"; Flags: ignoreversion

[Icons]
Name: "{group}\PULSO"; Filename: "{app}\PULSO.exe"
Name: "{group}\Crear paquete de soporte"; Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Support\support-bundle.ps1"""; WorkingDir: "{app}\Support"
Name: "{autodesktop}\PULSO"; Filename: "{app}\PULSO.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\PULSO.exe"; Description: "{cm:LaunchProgram,PULSO}"; Flags: nowait postinstall skipifsilent

[Code]
function AbletonSuiteRemoteRootExists: Boolean;
begin
  Result := DirExists(ExpandConstant('{commonappdata}\Ableton\Live 12 Suite\Resources\MIDI Remote Scripts'));
end;

function AbletonStandardRemoteRootExists: Boolean;
begin
  Result := DirExists(ExpandConstant('{commonappdata}\Ableton\Live 12 Standard\Resources\MIDI Remote Scripts'));
end;

function AbletonIntroRemoteRootExists: Boolean;
begin
  Result := DirExists(ExpandConstant('{commonappdata}\Ableton\Live 12 Intro\Resources\MIDI Remote Scripts'));
end;

function AbletonLiteRemoteRootExists: Boolean;
begin
  Result := DirExists(ExpandConstant('{commonappdata}\Ableton\Live 12 Lite\Resources\MIDI Remote Scripts'));
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if CheckForMutexes('Ableton Live') then
  begin
    MsgBox('Cierra Ableton Live antes de instalar PULSO. El instalador no cerrará ni reiniciará tu sesión automáticamente.', mbError, MB_OK);
    Result := False;
  end;
end;
