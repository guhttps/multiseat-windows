#ifndef ProductVersion
  #error ProductVersion must be supplied by Build-Release.ps1.
#endif
#ifndef ReleaseVersion
  #error ReleaseVersion must be supplied by Build-Release.ps1.
#endif
#ifndef PayloadRoot
  #error PayloadRoot must be supplied by Build-Release.ps1.
#endif
#ifndef ArtifactRoot
  #error ArtifactRoot must be supplied by Build-Release.ps1.
#endif

[Setup]
AppId={{C1E337A2-08A4-4E21-A79B-7823C25E21EC}
AppName=Multiseat Windows Prototype
AppVersion={#ReleaseVersion}
AppVerName=Multiseat Windows Prototype {#ReleaseVersion}
VersionInfoVersion={#ProductVersion}
AppPublisher=Multiseat Windows contributors
AppPublisherURL=https://github.com/guhttps/multiseat-windows
AppSupportURL=https://github.com/guhttps/multiseat-windows/issues
DefaultDirName={localappdata}\Programs\MultiseatPrototype
DefaultGroupName=Multiseat Windows Prototype
PrivilegesRequired=lowest
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#ArtifactRoot}
OutputBaseFilename=MultiseatPrototype-{#ReleaseVersion}-windows-x64-installer
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
TimeStampsInUTC=yes
TouchDate=2000-01-01
TouchTime=00:00:00
InfoBeforeFile={#PayloadRoot}\LEIA-ME.txt
UninstallDisplayIcon={app}\MultiseatPrototype.exe
CloseApplications=no
RestartApplications=no
ChangesAssociations=no
ChangesEnvironment=no
SetupLogging=no

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#PayloadRoot}\MultiseatPrototype.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\ms-diagnostic.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\ms-session-probe.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\ms-keyboard-probe.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\ms-seat-demo.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\LEIA-ME.txt"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\INNO-SETUP-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\manifest.json"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#PayloadRoot}\SHA256SUMS.txt"; DestDir: "{app}"; Flags: ignoreversion touch

[Icons]
Name: "{autoprograms}\Multiseat Windows Prototype"; Filename: "{app}\MultiseatPrototype.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\MultiseatPrototype.exe"; Description: "Abrir Multiseat Windows Prototype"; Flags: nowait postinstall skipifsilent
