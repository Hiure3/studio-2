; Studio Live Pro - Inno Setup 6 script. Built by build.ps1:  ISCC.exe Installer\StudioLivePro.iss
#define AppName "Studio Live Pro"
#define AppVersion "1.0.0"
#define AppExe "StudioLivePro.exe"

[Setup]
AppId={{6F1B2C94-5A7E-4D1B-9C33-5E10A4C0D7A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Studio Live Pro
DefaultDirName={autopf}\Studio Live Pro
DefaultGroupName=Studio Live Pro
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\Release
OutputBaseFilename=StudioLivePro-Setup
Compression=lzma2/ultra
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
WizardStyle=modern
DisableProgramGroupPage=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
Name: "{userdocs}\StudioLivePro"; Flags: uninsneveruninstall
Name: "{userdocs}\StudioLivePro\Logs"; Flags: uninsneveruninstall
Name: "{userdocs}\StudioLivePro\Projects"; Flags: uninsneveruninstall
Name: "{userdocs}\StudioLivePro\Presets"; Flags: uninsneveruninstall

[Files]
Source: "..\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Release\README.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\Release\USER_GUIDE.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\Release\INSTALLATION.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\Release\BUILD_REPORT.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\Release\TEST_REPORT.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion

[Icons]
Name: "{group}\Studio Live Pro"; Filename: "{app}\{#AppExe}"
Name: "{group}\User Guide"; Filename: "{app}\Documentation\USER_GUIDE.md"
Name: "{group}\Logs"; Filename: "{userdocs}\StudioLivePro\Logs"
Name: "{group}\Uninstall Studio Live Pro"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Studio Live Pro"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,Studio Live Pro}"; Flags: nowait postinstall skipifsilent

[Code]
// The application is built with the static MSVC runtime, so no Visual C++ Redistributable is required.
// ASIO drivers are provided by the audio interface vendor; WASAPI is part of Windows.
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsWin64 then begin
    MsgBox('Studio Live Pro requires 64-bit Windows 10 or 11.', mbError, MB_OK);
    Result := False;
  end;
end;
