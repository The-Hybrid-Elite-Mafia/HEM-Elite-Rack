; =============================================================================
;  HEM-Elite-Rack VST3 — plug-ins only (no rack app). For DAWs, and for the Microsoft Store
;  (which takes this installer as an EXE listing: silent install, versioned download address).
;  Built by CI:  ISCC /DAppVersion=x.y.z /DVst3Bundle=<path> /DVst3License=<path> HEMEliteRackVST3.iss
;  Licence: GPLv3 or later.
;  AppId is permanent: never change it, or upgrades will install side by side.
;  The plug-in files are shared with the full HEM-Elite-Rack installer: uninstalling one of the two
;  leaves them for the other; they go when neither is installed.
; =============================================================================
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

#define MyMark "PluginsOnly"
#define OtherMark "FullRack"
#define MyTask ""

[Setup]
AppId={{39392F67-816C-49F0-B399-A597A29CAC2A}
AppName=HEM-Elite-Rack VST3
AppVersion={#AppVersion}
AppVerName=HEM-Elite-Rack VST3 {#AppVersion}
AppPublisher=Hybrid Elite Mafia
AppPublisherURL=https://github.com/The-Hybrid-Elite-Mafia/HEM-Elite-Rack
DefaultDirName={autopf}\Hybrid Elite Mafia\HEM-Elite-Rack VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateUninstallRegKey=yes
OutputBaseFilename=HEM-Elite-Rack-VST3-Setup-{#AppVersion}
OutputDir=output
SetupIconFile=..\rackapp\resources\HEMEliteRack.ico
UninstallDisplayIcon={uninstallexe}
UninstallDisplayName=HEM-Elite-Rack VST3
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Files]
Source: "{#Vst3Bundle}\*";                                   DestDir: "{commoncf64}\VST3\HEM-Elite-Rack.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs uninsneveruninstall
Source: "{#Vst3License}";                                    DestDir: "{app}\licenses"; DestName: "Steinberg-VST3-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\LICENSE";                                        DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt";                           DestDir: "{app}"; Flags: ignoreversion
Source: "..\external\HybridCore\assets\fonts\OFL.txt";      DestDir: "{app}\licenses"; DestName: "Poppins-OFL.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\stb\LICENSE";      DestDir: "{app}\licenses"; DestName: "stb-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\dr_libs\LICENSE";  DestDir: "{app}\licenses"; DestName: "dr_libs-LICENSE.txt"; Flags: ignoreversion

[Registry]
; Marks that this product put the plug-ins in Common Files\VST3. Removed again on uninstall.
Root: HKLM; Subkey: "Software\Hybrid Elite Mafia\HEM-Elite-Rack VST3 owners"; ValueType: dword; ValueName: "{#MyMark}"; ValueData: 1; Flags: uninsdeletevalue uninsdeletekeyifempty{#MyTask}

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
