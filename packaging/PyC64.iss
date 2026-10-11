; Build with Inno Setup 7 after an MSVC or clang-cl x64 release build.
; Keep this AppId and version stable across rebuilds of the initial release.
#if VER < EncodeVer(7, 0, 0)
  #error Inno Setup 7 or newer is required.
#endif
#define AppVersion "0.1.0"
#define ProjectRoot AddBackslash(SourcePath) + ".."
#ifndef CompilerExe
  #define CompilerExe "build\windows\x64\release\pyc64c.exe"
#endif
#ifdef InstallerSmokeTest
  #define InstallerName "PyC64-installer-smoke-test"
#else
  #define InstallerName "PyC64-" + AppVersion + "-windows-x64-setup"
#endif

[Setup]
SourceDir={#ProjectRoot}
#ifdef InstallerSmokeTest
AppId={{1D707AB9-FCED-4F9E-A66C-D41304755193}
#else
AppId={{792B5F2C-1918-4876-84C3-56907439E053}
#endif
AppName=PyC64
AppVersion={#AppVersion}
AppPublisher=Aspenini
AppComments=Python subset and libraries compiled to native Commodore 64 programs
VersionInfoVersion={#AppVersion}.0
DefaultDirName=C:\PyC64
UsePreviousAppDir=no
DisableDirPage=yes
DefaultGroupName=PyC64
DisableProgramGroupPage=yes
#ifdef InstallerSmokeTest
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
UsePreviousPrivileges=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#ProjectRoot}\LICENSE
UninstallDisplayIcon={app}\bin\pyc64c.exe
OutputDir={#ProjectRoot}\build\installer
OutputBaseFilename={#InstallerName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#CompilerExe}"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "stdlib\*.py"; DestDir: "{app}\share\pyc64\stdlib"; Flags: ignoreversion recursesubdirs
Source: "stdlib\*.pyi"; DestDir: "{app}\share\pyc64\stdlib"; Flags: ignoreversion recursesubdirs
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "platform\vendor\STB_LICENSE.txt"; DestDir: "{app}\share\pyc64\licenses"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "examples\*"; DestDir: "{app}\examples"; Excludes: "build\*,*\build\*,__pycache__\*,*\__pycache__\*,*.exe,*.PRG,*.map,*.lst,*.pyc"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\PyC64 documentation"; Filename: "{app}\README.md"
Name: "{group}\PyC64 examples"; Filename: "{app}\examples"
Name: "{group}\Uninstall PyC64"; Filename: "{uninstallexe}"
