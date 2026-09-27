; Inno Setup script for Pyaw on Windows.  Build:  iscc windows\installer\pyaw.iss
; Expects the DLLs in build\win-msvc\{x64,x86}\Release and the model in windows\installer\model.
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif

[Setup]
AppId={{E05EC474-487B-4788-AD78-D238B56D2833}
AppName=Pyaw
AppVersion={#AppVersion}
AppVerName=Pyaw {#AppVersion}
AppPublisher=Pyaw
AppPublisherURL=https://github.com/DolphinsAreWeird/pyaw
AppSupportURL=https://github.com/DolphinsAreWeird/pyaw/issues
DefaultDirName={autopf}\Pyaw
DefaultGroupName=Pyaw
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=Output
OutputBaseFilename=Pyaw-{#AppVersion}-Windows-Setup
SetupIconFile=..\src\pyaw.ico
UninstallDisplayIcon={app}\pyaw64.dll
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
LicenseFile=..\..\LICENSE
CloseApplications=no

[Tasks]
Name: addkeyboard; Description: "Add Pyaw to my keyboards (under Burmese)"

[Files]
Source: "..\..\build\win-msvc\x64\Release\pyaw64.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "..\..\build\win-msvc\x86\Release\pyaw32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "model\model.bin"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "model\lexicon.tsv"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "keyboard.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "..\..\DATA.md"; DestDir: "{app}"; DestName: "DATA.txt"

[Icons]
Name: "{group}\Pyaw Settings"; Filename: "{sys}\rundll32.exe"; Parameters: """{app}\pyaw64.dll"",ShowSettings"; IconFilename: "{app}\pyaw64.dll"; Comment: "Options, My Words and help for the Pyaw keyboard"

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\keyboard.ps1"" add"; Flags: runhidden runasoriginaluser; Tasks: addkeyboard; StatusMsg: "Adding Pyaw to your keyboards..."
Filename: "{sys}\rundll32.exe"; Parameters: """{app}\pyaw64.dll"",ShowSettings"; Description: "Show how to use Pyaw"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\keyboard.ps1"" remove"; Flags: runhidden; RunOnceId: "RemovePyawKeyboard"
