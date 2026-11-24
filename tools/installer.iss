; Установщик MyCodeApp для Windows (Inno Setup 6).
; Собирается из той же папки, что и портативный ZIP (tools/package.ps1):
;   ISCC.exe /DMyAppVersion=1.0.4 /DStageDir=<...>\dist\MyCodeApp-1.0.4-win64 ^
;            /DOutputDir=<...>\dist tools\installer.iss
; Папка data из портативной сборки не ставится: в Program Files она недоступна
; для записи, поэтому настройки пойдут в профиль пользователя (обычный режим).

#ifndef MyAppVersion
  #define MyAppVersion "1.0.4"
#endif
#ifndef StageDir
  #define StageDir "..\dist\MyCodeApp-" + MyAppVersion + "-win64"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{5D6C2A64-9F0B-4C1E-9E7A-3B7A0B8C41D2}
AppName=MyCodeApp
AppVersion={#MyAppVersion}
AppVerName=MyCodeApp {#MyAppVersion}
AppPublisher=MyCodeApp
AppPublisherURL=https://github.com/Alexey-Izilianov/MyCodeApp
DefaultDirName={autopf}\MyCodeApp
DefaultGroupName=MyCodeApp
DisableProgramGroupPage=yes
; Установщик кладёт программу в Program Files — нужны права администратора
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=MyCodeApp-{#MyAppVersion}-win64-setup
SetupIconFile=..\assets\icon\app.ico
UninstallDisplayIcon={app}\appMyCodeApp.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
VersionInfoVersion={#MyAppVersion}.0

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Дополнительно:"; Flags: unchecked

[Files]
; data\ исключена: портативный режим хранит настройки рядом с exe и в Program
; Files писать нельзя. Модель (models\*.gguf), если она есть в сборке, едет в пакет.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "data\*"

[Icons]
Name: "{group}\MyCodeApp"; Filename: "{app}\appMyCodeApp.exe"
Name: "{group}\Удалить MyCodeApp"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MyCodeApp"; Filename: "{app}\appMyCodeApp.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\appMyCodeApp.exe"; Description: "Запустить MyCodeApp"; Flags: nowait postinstall skipifsilent
