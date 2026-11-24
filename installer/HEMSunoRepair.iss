; =============================================================================
;  HEM Suno Repair — Windows installer (Inno Setup 6). Built by .github/workflows/suno-repair-app.yml:
;    ISCC /DAppVersion=x.y.z /DAppDir=<PyInstaller dist folder> HEMSunoRepair.iss
;  Installs for the current user only (no admin prompt). AppId is permanent.
;  Settings (incl. the Claude API key) live in %APPDATA%\HEM Suno Repair and are never touched here.
; =============================================================================
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppDir
  #define AppDir "..\tools\suno_repair\dist\HEM Suno Repair"
#endif

[Setup]
AppId={{E6F5C77F-0BD1-47DA-9065-18E6C1F0F239}
AppName=HEM Suno Repair
AppVersion={#AppVersion}
AppVerName=HEM Suno Repair {#AppVersion}
AppPublisher=Hybrid Elite Mafia
DefaultDirName={localappdata}\Programs\HEM Suno Repair
DisableProgramGroupPage=yes
DefaultGroupName=HEM Suno Repair
OutputBaseFilename=HEM-Suno-Repair-Setup
OutputDir=output
SetupIconFile=..\rackapp\resources\HEMEliteRack.ico
UninstallDisplayIcon={app}\HEM Suno Repair.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\HEM Suno Repair"; Filename: "{app}\HEM Suno Repair.exe"
Name: "{autodesktop}\HEM Suno Repair"; Filename: "{app}\HEM Suno Repair.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\HEM Suno Repair.exe"; Description: "Open HEM Suno Repair now"; Flags: nowait postinstall skipifsilent
