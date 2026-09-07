#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef SourceDir
  #error SourceDir must point to the deployed application directory
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef AppIcon
  #error AppIcon must point to the application ICO file
#endif

[Setup]
AppId={{F09A17B4-53AD-47F7-93EA-B9FE4183BC34}
AppName=AMR Map Manager
AppVersion={#AppVersion}
AppPublisher=SVI
AppPublisherURL=https://github.com/HenryZhu-svi/amr_map_change_one_click
DefaultDirName={autopf}\SVI\AMR Map Manager
DefaultGroupName=SVI\AMR Map Manager
OutputDir={#OutputDir}
OutputBaseFilename=AMRMapManager-{#AppVersion}-win64-setup
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\amr-map-manager.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "VC_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\AMR Map Manager"; Filename: "{app}\amr-map-manager.exe"
Name: "{autodesktop}\AMR Map Manager"; Filename: "{app}\amr-map-manager.exe"; Tasks: desktopicon

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated
Filename: "{app}\amr-map-manager.exe"; Description: "{cm:LaunchProgram,AMR Map Manager}"; Flags: nowait postinstall skipifsilent
