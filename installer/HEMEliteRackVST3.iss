; =============================================================================
;  HEM-Elite-Rack VST3 — plug-ins only (no rack app). For DAWs, and for the Microsoft Store
;  (which takes this installer as an EXE listing: silent install, versioned download address).
;  Built by CI:  ISCC /DAppVersion=x.y.z /DVst3Bundle=<path> /DVst3License=<path> HEMEliteRackVST3.iss
;  Licence: GPLv3 or later.
;  AppId is permanent: never change it, or upgrades will install side by side.
;  The plug-in files are marked shared: if the full HEM-Elite-Rack installer also put them there,
;  uninstalling one of the two leaves them for the other.
; =============================================================================
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

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
Source: "{#Vst3Bundle}\*";                                   DestDir: "{commoncf64}\VST3\HEM-Elite-Rack.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs sharedfile uninsnosharedfileprompt
Source: "{#Vst3License}";                                    DestDir: "{app}\licenses"; DestName: "Steinberg-VST3-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\LICENSE";                                        DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt";                           DestDir: "{app}"; Flags: ignoreversion
Source: "..\external\HybridCore\assets\fonts\OFL.txt";      DestDir: "{app}\licenses"; DestName: "Poppins-OFL.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\stb\LICENSE";      DestDir: "{app}\licenses"; DestName: "stb-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\HybridCore\external\dr_libs\LICENSE";  DestDir: "{app}\licenses"; DestName: "dr_libs-LICENSE.txt"; Flags: ignoreversion
