Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef APP_VERSION
  !error "Pass /DAPP_VERSION=X.Y.Z"
!endif
!ifndef SOURCE_ROOT
  !error "Pass /DSOURCE_ROOT=path"
!endif
!ifndef ARTIFACT_ROOT
  !error "Pass /DARTIFACT_ROOT=path"
!endif
!ifndef OUTPUT_ROOT
  !error "Pass /DOUTPUT_ROOT=path"
!endif

Name "PULSO ${APP_VERSION}"
OutFile "${OUTPUT_ROOT}\PULSO-${APP_VERSION}-windows-x64-setup.exe"
InstallDir "$PROGRAMFILES64\PULSO"
InstallDirRegKey HKLM "Software\PULSO Music\PULSO" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "PULSO"
VIAddVersionKey "FileDescription" "PULSO installer (unsigned beta)"
VIAddVersionKey "CompanyName" "PULSO Music"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright PULSO Music"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "PULSO ${APP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Install PULSO VST3 and standalone for Windows x64.$\r$\n$\r$\nThis beta installer is not digitally signed. Windows SmartScreen may ask you to confirm that you want to run it. Close Ableton Live before continuing."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE_ROOT}\LICENSE.md"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

LangString OLD_INNO ${LANG_ENGLISH} "An older PULSO installer was found. Uninstall it from Windows Installed Apps, then run this setup again. Your PULSO settings are not removed."
LangString OLD_INNO ${LANG_SPANISH} "Se encontró una instalación anterior de PULSO. Desinstálala desde Aplicaciones instaladas de Windows y vuelve a ejecutar este instalador. La configuración de PULSO se conserva."
LangString LIVE_OPEN ${LANG_ENGLISH} "Close Ableton Live before installing PULSO, then run this setup again."
LangString LIVE_OPEN ${LANG_SPANISH} "Cierra Ableton Live antes de instalar PULSO y vuelve a ejecutar este instalador."

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP|MB_OK "PULSO requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\{58C16D30-17B4-4E8A-BEBE-5C57F94A31D8}_is1" "UninstallString"
  StrCmp $0 "" +3
    MessageBox MB_ICONSTOP|MB_OK "$(OLD_INNO)"
    Abort
  System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "Ableton Live") p .r0'
  StrCmp $0 0 +4
    System::Call 'kernel32::CloseHandle(p r0)'
    MessageBox MB_ICONSTOP|MB_OK "$(LIVE_OPEN)"
    Abort
FunctionEnd

Section "PULSO VST3 + standalone (required)" SEC_CORE
  SectionIn RO
  SetRegView 64
  SetShellVarContext all
  SetOutPath "$COMMONFILES64\VST3\PULSO.vst3"
  File /r /x "*.incomplete.*" "${ARTIFACT_ROOT}\VST3\PULSO.vst3\*"
  SetOutPath "$INSTDIR"
  File "${ARTIFACT_ROOT}\Standalone\PULSO.exe"
  SetOutPath "$INSTDIR\Documentation"
  File "${SOURCE_ROOT}\README.md"
  File "${SOURCE_ROOT}\LICENSE.md"
  File "${SOURCE_ROOT}\docs\*.md"
  SetOutPath "$INSTDIR\Support"
  File "${SOURCE_ROOT}\scripts\support-bundle.ps1"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall-PULSO.exe"
  CreateDirectory "$SMPROGRAMS\PULSO"
  CreateShortcut "$SMPROGRAMS\PULSO\PULSO.lnk" "$INSTDIR\PULSO.exe"
  CreateShortcut "$SMPROGRAMS\PULSO\Uninstall PULSO.lnk" "$INSTDIR\Uninstall-PULSO.exe"
  WriteRegStr HKLM "Software\PULSO Music\PULSO" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "DisplayName" "PULSO ${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "Publisher" "PULSO Music"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "DisplayIcon" "$INSTDIR\PULSO.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "UninstallString" "$\"$INSTDIR\Uninstall-PULSO.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "QuietUninstallString" "$\"$INSTDIR\Uninstall-PULSO.exe$\" /S"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "URLInfoAbout" "https://pulso.cargo-core.com"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO" "NoRepair" 1
SectionEnd

!macro InstallBridge EDITION TOKEN
  IfFileExists "$APPDATA\Ableton\${EDITION}\Resources\MIDI Remote Scripts\*.*" 0 bridge_done_${TOKEN}
  SetOutPath "$APPDATA\Ableton\${EDITION}\Resources\MIDI Remote Scripts\PulsoDeployRemote"
  File "${SOURCE_ROOT}\ableton\PulsoDeployRemote\*.py"
  bridge_done_${TOKEN}:
!macroend

Section "Ableton Live 12 control surface" SEC_BRIDGE
  !insertmacro InstallBridge "Live 12 Suite" suite
  !insertmacro InstallBridge "Live 12 Standard" standard
  !insertmacro InstallBridge "Live 12 Intro" intro
  !insertmacro InstallBridge "Live 12 Lite" lite
  WriteRegDWORD HKLM "Software\PULSO Music\PULSO" "BridgeInstalled" 1
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortcut "$DESKTOP\PULSO.lnk" "$INSTDIR\PULSO.exe"
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  Delete "$DESKTOP\PULSO.lnk"
  RMDir /r "$SMPROGRAMS\PULSO"
  RMDir /r "$COMMONFILES64\VST3\PULSO.vst3"
  ReadRegDWORD $0 HKLM "Software\PULSO Music\PULSO" "BridgeInstalled"
  IntCmp $0 1 bridge_done 0 bridge_done
    RMDir /r "$APPDATA\Ableton\Live 12 Suite\Resources\MIDI Remote Scripts\PulsoDeployRemote"
    RMDir /r "$APPDATA\Ableton\Live 12 Standard\Resources\MIDI Remote Scripts\PulsoDeployRemote"
    RMDir /r "$APPDATA\Ableton\Live 12 Intro\Resources\MIDI Remote Scripts\PulsoDeployRemote"
    RMDir /r "$APPDATA\Ableton\Live 12 Lite\Resources\MIDI Remote Scripts\PulsoDeployRemote"
  bridge_done:
  RMDir /r "$INSTDIR\Documentation"
  RMDir /r "$INSTDIR\Support"
  Delete "$INSTDIR\PULSO.exe"
  Delete "$INSTDIR\Uninstall-PULSO.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PULSO"
  DeleteRegKey HKLM "Software\PULSO Music\PULSO"
SectionEnd
