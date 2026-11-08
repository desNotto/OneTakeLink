; OneTakeLink installer (Inno Setup 6). Built by .github/workflows/build.yml:
;   ISCC /DAppVersion=1.0.0 /DSourceDir=dist\OneTakeLink /DOutputDir=dist packaging\windows\onetakelink.iss
; Saved as UTF-8 with BOM (Russian text below).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\OneTakeLink"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
; Never change AppId: it identifies the installation for upgrades/uninstall.
AppId={{6F1B2C0E-7A43-4C8B-9E55-0B7D1F2A9C31}
AppName=OneTakeLink
AppVersion={#AppVersion}
AppVerName=OneTakeLink {#AppVersion}
AppPublisher=desNotto
AppPublisherURL=https://github.com/desNotto/onetakelink
AppSupportURL=https://github.com/desNotto/onetakelink/issues
AppUpdatesURL=https://github.com/desNotto/onetakelink/releases
DefaultDirName={autopf}\OneTakeLink
DefaultGroupName=OneTakeLink
DisableProgramGroupPage=yes
; All users (Program Files, needs admin) or just me (no admin rights).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\assets\icon\onetakelink.ico
UninstallDisplayIcon={app}\OneTakeLink.exe
UninstallDisplayName=OneTakeLink
OutputDir={#OutputDir}
OutputBaseFilename=OneTakeLink-{#AppVersion}-setup-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoProductName=OneTakeLink
VersionInfoCompany=desNotto

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.FirewallTask=Разрешить OneTakeLink в брандмауэре Windows (частные сети) — нужно для подключения iPhone по Wi-Fi
en.FirewallTask=Allow OneTakeLink in Windows Firewall (private networks) — needed to connect an iPhone over Wi-Fi
ru.LaunchApp=Запустить OneTakeLink
en.LaunchApp=Launch OneTakeLink
ru.CliShortcut=Диагностика подключения (otl-cli)
en.CliShortcut=Connection diagnostics (otl-cli)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "firewall"; Description: "{cm:FirewallTask}"; Check: IsAdminInstallMode

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\OneTakeLink"; Filename: "{app}\OneTakeLink.exe"
Name: "{autodesktop}\OneTakeLink"; Filename: "{app}\OneTakeLink.exe"; Tasks: desktopicon

[Registry]
; UI language of the app follows the installer language on first install
; (only if the user has not chosen one in the app yet).
Root: HKCU; Subkey: "Software\OneTakeLink\Installer"; ValueType: string; ValueName: "Language"; ValueData: "{language}"; Flags: uninsdeletekey

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OneTakeLink"" program=""{app}\OneTakeLink.exe"""; Flags: runhidden; Tasks: firewall
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""OneTakeLink"" dir=in action=allow program=""{app}\OneTakeLink.exe"" enable=yes profile=private"; Flags: runhidden; Tasks: firewall
Filename: "{app}\OneTakeLink.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OneTakeLink"" program=""{app}\OneTakeLink.exe"""; Flags: runhidden; RunOnceId: "RemoveFirewallRule"; Check: IsAdminInstallMode

[UninstallDelete]
; App data: settings, thumbnail cache, logs. Recordings are never touched.
Type: filesandordirs; Name: "{localappdata}\OneTakeLink"
