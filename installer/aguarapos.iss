; AguaraPOS: the installer, and with /DPatch the patch that updates a till.
; Built by build.sh, which fills the staging folder and passes the version.
;
;   ISCC /DAppVersion=1.0.0 /DStage=C:\...\stage aguarapos.iss
;   ISCC /DAppVersion=1.0.1 /DStage=C:\...\stage /DPatch aguarapos.iss

#ifndef AppVersion
  #error Pass /DAppVersion
#endif
#ifndef Stage
  #error Pass /DStage
#endif

; Never change it: it's how a patch finds the installation.
#define AppGuid "{{7D0C2C1E-6A5B-4F4E-9C51-A6F1D1B2E3A7}"

[Setup]
AppId={#AppGuid}
AppName=AguaraPOS
AppVersion={#AppVersion}
AppPublisher=Aguara
VersionInfoVersion={#AppVersion}
; Windows 7 with Service Pack 1, the oldest system a till can have
MinVersion=6.1sp1
PrivilegesRequired=admin
DefaultDirName={autopf}\AguaraPOS
DefaultGroupName=AguaraPOS
UninstallDisplayIcon={app}\aguara.exe
OutputDir=output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; aguara.exe holds this mutex. The program is never closed from here: with the
; till open, leaving takes closing it or a supervisor.
AppMutex=AguaraPOS-running
CloseApplications=no
#ifdef Patch
OutputBaseFilename=aguarapos-parche-{#AppVersion}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
CreateUninstallRegKey=no
UpdateUninstallLogAppend=yes
#else
OutputBaseFilename=aguarapos-{#AppVersion}
#endif

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

#ifndef Patch
[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"
#endif

[InstallDelete]
; A module that left the program must not stay behind and fail to load.
Type: files; Name: "{app}\modules\*.dll"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

#ifndef Patch
[Icons]
Name: "{group}\AguaraPOS"; Filename: "{app}\aguara.exe"
Name: "{group}\Manual del cajero"; Filename: "{app}\doc\manual-cajero.pdf"
Name: "{group}\Manual del encargado"; Filename: "{app}\doc\manual-encargado.pdf"
Name: "{group}\Desinstalar AguaraPOS"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AguaraPOS"; Filename: "{app}\aguara.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\aguara.exe"; Description: "Abrir AguaraPOS"; Flags: postinstall nowait skipifsilent
#endif

[Code]
#ifdef Patch
function InitializeSetup(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGuid}_is1');
  if not Result then
    MsgBox('AguaraPOS no está instalado en este equipo. Use el instalador completo.', mbError, MB_OK)
  else
    Result := MsgBox('Antes de seguir:' + #13#10#13#10 +
                     '1. Cierre la caja y salga de AguaraPOS en TODOS los equipos.' + #13#10 +
                     '2. Haga una copia de seguridad de los datos.' + #13#10 +
                     '3. Empiece por este equipo y aplique el parche a todos.' + #13#10#13#10 +
                     'Un equipo sin actualizar no podrá entrar hasta recibir el parche.' + #13#10#13#10 +
                     '¿Continuar?', mbConfirmation, MB_YESNO) = IDYES;
end;
#endif
