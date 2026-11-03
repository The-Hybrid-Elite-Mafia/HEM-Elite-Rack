; =============================================================================
;  HEM-Elite-Rack — Windows installer (Inno Setup 6)
;  Built by CI:  ISCC /DAppVersion=x.y.z /DBinDir=<build dir> [/DVst3Bundle=<path> /DVst3License=<path>] HEMEliteRack.iss
;  Licence: GPLv3 or later. Sound: ASIO (Steinberg's ASIO SDK under its GPLv3 option) or WASAPI.
;  AppId is permanent: never change it, or upgrades will install side by side.
; =============================================================================
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef BinDir
  #define BinDir "..\build\rack"
#endif

#define MyMark "FullRack"
#define OtherMark "PluginsOnly"
#define MyTask "; Tasks: vst3"

[Setup]
AppId={{8B6CFE21-FA73-4B6D-B7CD-612B8361AD6E}
AppName=HEM-Elite-Rack
AppVersion={#AppVersion}
AppVerName=HEM-Elite-Rack {#AppVersion}
AppPublisher=Hybrid Elite Mafia
AppPublisherURL=https://github.com/The-Hybrid-Elite-Mafia/HEM-Elite-Rack
DefaultDirName={autopf}\Hybrid Elite Mafia\HEM-Elite-Rack
DefaultGroupName=HEM-Elite-Rack
DisableProgramGroupPage=yes
OutputBaseFilename=HEM-Elite-Rack-Setup-{#AppVersion}
OutputDir=output
SetupIconFile=..\rackapp\resources\HEMEliteRack.ico
UninstallDisplayIcon={app}\HEMEliteRack.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=HEM-Elite-Rack
CloseApplications=yes

; Settings live in %APPDATA%\HEM-Elite-Rack (settings.ini: your chain, output, folders).
; This installer never creates, overwrites or deletes that folder, so they survive upgrades and uninstall.

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Shortcuts:"
#ifdef Vst3Bundle
Name: "vst3"; Description: "Install the VST3 plug-ins (all HEM-Elite-Rack plug-ins) into Common Files\VST3"; GroupDescription: "Plug-ins:"
#endif

[Files]
Source: "{#BinDir}\rackapp\Release\HEMEliteRack.exe";                   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\rackapp\Release\assets\*";                          DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\tests\Release\hem_rack_tests.exe";                   DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#BinDir}\tools\rack_snapshot\Release\hem_rack_snapshot.exe";  DestDir: "{app}\bin"; Flags: ignoreversion
; the self-test reads the faceplate pictures from the app's own assets folder (no second copy)
Source: "{#BinDir}\tests\Release\assets\*";                            DestDir: "{app}\bin\assets"; Excludes: "plates\*,cards\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "HOW-TO-USE.txt";                                                DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt";                                       DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE";                                                    DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
#ifdef AsioLicense
Source: "{#AsioLicense}";                                                DestDir: "{app}\licenses"; DestName: "Steinberg-ASIO-SDK-LICENSE.txt"; Flags: ignoreversion
#endif
Source: "..\external\HybridCore\assets\fonts\OFL.txt";                  DestDir: "{app}\licenses"; DestName: "Poppins-OFL.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\stb\LICENSE";                  DestDir: "{app}\licenses"; DestName: "stb-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\dr_libs\LICENSE";              DestDir: "{app}\licenses"; DestName: "dr_libs-LICENSE.txt"; Flags: ignoreversion
#ifdef Vst3Bundle
Source: "{#Vst3Bundle}\*";  DestDir: "{commoncf64}\VST3\HEM-Elite-Rack.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs uninsneveruninstall; Tasks: vst3
Source: "{#Vst3License}";   DestDir: "{app}\licenses"; DestName: "Steinberg-VST3-SDK-LICENSE.txt"; Flags: ignoreversion
#endif

#ifdef Vst3Bundle
[Registry]
; Marks that this product put the plug-ins in Common Files\VST3. Removed again on uninstall.
Root: HKLM; Subkey: "Software\Hybrid Elite Mafia\HEM-Elite-Rack VST3 owners"; ValueType: dword; ValueName: "{#MyMark}"; ValueData: 1; Flags: uninsdeletevalue uninsdeletekeyifempty{#MyTask}
#endif

[Icons]
Name: "{group}\HEM-Elite-Rack";               Filename: "{app}\HEMEliteRack.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\HEM-Elite-Rack";         Filename: "{app}\HEMEliteRack.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{group}\Self-test";                    Filename: "{cmd}"; Parameters: "/k """"{app}\bin\hem_rack_tests.exe"""""; WorkingDir: "{app}\bin"
Name: "{group}\How to use";                   Filename: "{app}\HOW-TO-USE.txt"
Name: "{group}\Uninstall HEM-Elite-Rack";     Filename: "{uninstallexe}"

[Run]
Filename: "{app}\HEMEliteRack.exe"; Description: "Start HEM-Elite-Rack"; Flags: postinstall nowait skipifsilent

#ifdef Vst3Bundle
[Code]
// The plug-in files are never removed by the normal uninstall step (uninsneveruninstall),
// because the full HEM-Elite-Rack installer and the plug-ins-only installer share them.
// After uninstall: if the other product's mark is gone too, delete the plug-in folder.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if not RegValueExists(HKLM, 'Software\Hybrid Elite Mafia\HEM-Elite-Rack VST3 owners', '{#OtherMark}') then
      DelTree(ExpandConstant('{commoncf64}\VST3\HEM-Elite-Rack.vst3'), True, True, True);
  end;
end;
#endif
