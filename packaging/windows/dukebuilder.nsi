; Compile through scripts/build-windows-installer.py, which supplies the payload manifest.
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "LogicLib.nsh"
!include "${CONFIG_FILE}"

!define APP_KEY "Software\DukeBuilderInstaller"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DukeBuilder"
Name "Duke Builder"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Duke Builder"
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetOverwrite on
VIProductVersion "${NUMERIC_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "Duke Builder"
VIAddVersionKey /LANG=1033 "FileDescription" "Duke Builder installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Duke Builder contributors"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Duke Builder requires 64-bit Windows."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "Duke Builder requires Windows 10 or newer."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext current
FunctionEnd

Section "Duke Builder (required)" MainSection
  SectionIn RO
  ; Remove a previous payload with its own file list, including obsolete DLLs.
  ; User settings and files are deliberately outside the installer registry key.
  ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
  ${If} $0 != ""
    IfFileExists "$0\Uninstall.exe" 0 missing_uninstaller
    ClearErrors
    ExecWait '"$0\Uninstall.exe" /S _?=$0' $1
    ${If} ${Errors}
      MessageBox MB_OK|MB_ICONSTOP "Unable to uninstall the previous version. Close Duke Builder and try again."
      Abort
    ${EndIf}
    ${If} $1 != 0
      MessageBox MB_OK|MB_ICONSTOP "The previous version could not be removed. Close Duke Builder and try again."
      Abort
    ${EndIf}
    Delete "$0\Uninstall.exe"
    RMDir "$0"
    Goto previous_removed
    missing_uninstaller:
    MessageBox MB_OK|MB_ICONSTOP "The previous installation's uninstaller is missing. Repair that installation before upgrading."
    Abort
    previous_removed:
  ${EndIf}
  SetOutPath "$INSTDIR"
  !insertmacro InstallPayload
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Duke Builder"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\dukebuilder.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_KIB}
  CreateDirectory "$SMPROGRAMS\Duke Builder"
  CreateShortcut "$SMPROGRAMS\Duke Builder\Duke Builder.lnk" "$INSTDIR\dukebuilder.exe"
  CreateShortcut "$SMPROGRAMS\Duke Builder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" DesktopSection
  CreateShortcut "$DESKTOP\Duke Builder.lnk" "$INSTDIR\dukebuilder.exe"
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
FunctionEnd

Section "Uninstall"
  ; Fail before removing dependencies if the application is still running.
  ClearErrors
  Delete "$INSTDIR\dukebuilder.exe"
  ${If} ${Errors}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Close Duke Builder before uninstalling."
    SetErrorLevel 1
    Quit
  ${EndIf}
  !insertmacro UninstallPayload
  Delete "$DESKTOP\Duke Builder.lnk"
  Delete "$SMPROGRAMS\Duke Builder\Duke Builder.lnk"
  Delete "$SMPROGRAMS\Duke Builder\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Duke Builder"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  ; Never recursively delete the installation directory: it may contain maps.
  RMDir "$INSTDIR"
  SetErrorLevel 0
SectionEnd
