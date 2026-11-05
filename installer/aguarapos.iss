; AguaraPOS: the installer, and with /DPatch the patch that updates a till.
; Keep the BOM: without it Inno reads the Spanish text in the system code page.
; Built by build.sh, which fills the staging folder and passes the version.
;
;   ISCC /DAppVersion=1.0.0-beta /DNumericVersion=1.0.0 /DStage=C:\...\stage /DPgStage=C:\...\stage-pg aguarapos.iss
;   ISCC /DAppVersion=1.0.1 /DNumericVersion=1.0.1 /DStage=C:\...\stage /DPatch aguarapos.iss

#ifndef AppVersion
  #error Pass /DAppVersion
#endif
#ifndef NumericVersion
  #error Pass /DNumericVersion
#endif
#ifndef Stage
  #error Pass /DStage
#endif

; Never change it: it's how a patch finds the installation.
#define AppGuid "7D0C2C1E-6A5B-4F4E-9C51-A6F1D1B2E3A7"
; [Setup] writes a literal brace as {{
#define SetupId "{{" + AppGuid + "}"

[Setup]
AppId={#SetupId}
AppName=AguaraPOS
AppVersion={#AppVersion}
AppPublisher=Aguara
; Windows only takes numbers here, so a label like -beta stays out
VersionInfoVersion={#NumericVersion}
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
#else
OutputBaseFilename=aguarapos-{#AppVersion}
#endif

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

#ifndef Patch
[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"
Name: "pgserver"; Description: "Instalar el servidor PostgreSQL (solo en la caja que guarda los datos)"; Flags: unchecked
#endif

[InstallDelete]
; A module that left the program must not stay behind and fail to load.
Type: files; Name: "{app}\modules\*.dll"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifndef Patch
Source: "{#PgStage}\*"; DestDir: "{app}\pgsql"; Tasks: pgserver; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

#ifndef Patch
[Icons]
Name: "{group}\AguaraPOS"; Filename: "{app}\aguara.exe"
Name: "{group}\Manual del cajero"; Filename: "{app}\doc\manual-cajero.pdf"
Name: "{group}\Manual del encargado"; Filename: "{app}\doc\manual-encargado.pdf"
Name: "{group}\Desinstalar AguaraPOS"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AguaraPOS"; Filename: "{app}\aguara.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\aguara.exe"; Description: "Abrir AguaraPOS"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; The service goes. The data stays, in ProgramData: it's the store's.
Filename: "{cmd}"; Parameters: "/S /C """"{app}\scripts\pg-setup.cmd"" remove ""{app}\pgsql\bin"""""; Tasks: pgserver; RunOnceId: "RemovePostgreSQL"; Flags: runhidden
#endif

[Code]
#ifdef Patch
function InitializeSetup(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + '{#AppGuid}' + '}_is1');
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
#else
var
  PasswordPage: TInputQueryWizardPage;

function DataDir(): String;
begin
  Result := ExpandConstant('{commonappdata}\AguaraPOS\pgdata');
end;

function ServerIsSetUp(): Boolean;
begin
  Result := FileExists(DataDir() + '\aguara-lista.txt');
end;

// Letters, digits and a few signs: it goes through cmd.exe and into SQL.
function PasswordIsPlain(const Password: String): Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 1 to Length(Password) do
    if Pos(Password[I], 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.@#') = 0 then
      Result := False;
end;

procedure InitializeWizard();
begin
  PasswordPage := CreateInputQueryPage(wpSelectTasks, 'Servidor PostgreSQL',
    'Contraseña de los datos de AguaraPOS',
    'Todas las cajas la usan para conectarse. Anótela y guárdela: hace falta en cada caja. ' +
    'Al menos 8 caracteres: letras sin acentos, números y _ - . @ #');
  PasswordPage.Add('Contraseña:', True);
  PasswordPage.Add('Repita la contraseña:', True);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = PasswordPage.ID) and (ServerIsSetUp() or not WizardIsTaskSelected('pgserver'));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Password: String;
begin
  Result := True;
  if CurPageID <> PasswordPage.ID then
    Exit;
  Password := PasswordPage.Values[0];
  if (Length(Password) < 8) or not PasswordIsPlain(Password) then begin
    MsgBox('Use al menos 8 caracteres: letras sin acentos, números y _ - . @ #', mbError, MB_OK);
    Result := False;
  end else if Password <> PasswordPage.Values[1] then begin
    MsgBox('Las dos contraseñas no coinciden.', mbError, MB_OK);
    Result := False;
  end;
end;

// Reinstalling: the service holds its files.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if WizardIsTaskSelected('pgserver') then
    Exec(ExpandConstant('{sys}\net.exe'), 'stop AguaraPostgreSQL', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

function Quoted(const Value: String): String;
begin
  Result := '"' + Value + '"';
end;

function ServerStep(const Arguments: String): Boolean;
var
  Code: Integer;
begin
  Result := Exec(ExpandConstant('{cmd}'),
                 '/S /C "' + Quoted(ExpandConstant('{app}\scripts\pg-setup.cmd')) + ' ' + Arguments + '"',
                 '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

procedure SetUpServer();
var
  Bin, Password, PasswordFile, SqlFile: String;
  Fresh, Done: Boolean;
begin
  WizardForm.StatusLabel.Caption := 'Preparando el servidor PostgreSQL...';
  Bin := Quoted(ExpandConstant('{app}\pgsql\bin'));
  Fresh := not ServerIsSetUp();
  PasswordFile := ExpandConstant('{tmp}\pg-password.txt');
  SqlFile := ExpandConstant('{tmp}\pg-setup.sql');
  if Fresh then begin
    Password := PasswordPage.Values[0];
    SaveStringToFile(PasswordFile, Password, False);
    SaveStringToFile(SqlFile, 'CREATE ROLE aguara LOGIN PASSWORD ''' + Password + ''';' + #13#10 +
                     'CREATE DATABASE aguara OWNER aguara ENCODING ''UTF8'' TEMPLATE template0;' + #13#10, False);
  end;
  Done := (not Fresh or ServerStep('init ' + Bin + ' ' + Quoted(DataDir()) + ' ' + Quoted(PasswordFile)))
          and ServerStep('service ' + Bin + ' ' + Quoted(DataDir()))
          and (not Fresh or ServerStep('database ' + Bin + ' ' + Quoted(DataDir()) + ' ' +
                                       Quoted(PasswordFile) + ' ' + Quoted(SqlFile)));
  DeleteFile(PasswordFile);
  DeleteFile(SqlFile);
  if not Done then
    SuppressibleMsgBox('AguaraPOS quedó instalado, pero el servidor PostgreSQL no se pudo preparar.' + #13#10#13#10 +
                       'El detalle está en ' + ExpandConstant('{commonappdata}\AguaraPOS\pg-setup.log') + #13#10#13#10 +
                       'Si ya hay otro PostgreSQL en este equipo, desinstálelo y vuelva a ejecutar este instalador ' +
                       'con la misma contraseña.', mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('pgserver') then
    SetUpServer();
end;
#endif
