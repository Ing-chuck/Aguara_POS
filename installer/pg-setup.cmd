@echo off
rem Sets up the PostgreSQL server that comes with AguaraPOS. The installer runs
rem it step by step; what happens stays in %ProgramData%\AguaraPOS\pg-setup.log.
rem
rem   pg-setup init BIN DATA PWFILE         create the data folder, once
rem   pg-setup service BIN DATA             service and firewall, and start it
rem   pg-setup database BIN DATA PWFILE SQL the aguara user and database
rem   pg-setup remove BIN                   take out the service, not the data
setlocal
set "SERVICE=AguaraPostgreSQL"
set "RULE=AguaraPOS PostgreSQL"
if "%PGPORT%"=="" set "PGPORT=5432"
set "STEP=%~1"
set "BIN=%~2"
set "LOGDIR=%ProgramData%\AguaraPOS"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\pg-setup.log"
>>"%LOG%" echo === %DATE% %TIME% %STEP%

if /i "%STEP%"=="init" goto init
if /i "%STEP%"=="service" goto service
if /i "%STEP%"=="database" goto database
if /i "%STEP%"=="remove" goto remove
>>"%LOG%" echo Unknown step: %STEP%
exit /b 2

:init
set "DATA=%~3"
if exist "%DATA%\PG_VERSION" (
    >>"%LOG%" echo Data folder already there: left as it is.
    exit /b 0
)
rem Something else on this port, such as an earlier PostgreSQL, would take the
rem connections meant for this server.
"%BIN%\pg_isready.exe" -h localhost -p %PGPORT% >>"%LOG%" 2>&1
if not errorlevel 1 (
    >>"%LOG%" echo Port %PGPORT% is already in use by a PostgreSQL server.
    exit /b 3
)
"%BIN%\initdb.exe" -D "%DATA%" -U postgres --pwfile="%~4" -A scram-sha-256 --encoding=UTF8 --locale=Spanish_Paraguay.1252 --lc-messages=C >>"%LOG%" 2>&1
if errorlevel 1 exit /b 4
rem Before any password is stored: one in md5 wouldn't pass the rule below.
>>"%DATA%\postgresql.conf" echo listen_addresses = '*'
>>"%DATA%\postgresql.conf" echo password_encryption = scram-sha-256
>>"%DATA%\postgresql.conf" echo logging_collector = on
>>"%DATA%\pg_hba.conf" echo host    aguara    aguara    samenet    scram-sha-256
exit /b 0

:service
set "DATA=%~3"
rem By SID: the name of this account depends on the language of Windows.
icacls "%DATA%" /grant "*S-1-5-20:(OI)(CI)F" /T /Q >>"%LOG%" 2>&1
if errorlevel 1 exit /b 5
sc query %SERVICE% >nul 2>&1
if errorlevel 1 (
    "%BIN%\pg_ctl.exe" register -N %SERVICE% -D "%DATA%" -S auto -U "NT AUTHORITY\NetworkService" >>"%LOG%" 2>&1
    if errorlevel 1 exit /b 6
)
netsh advfirewall firewall delete rule name="%RULE%" >nul 2>&1
netsh advfirewall firewall add rule name="%RULE%" dir=in action=allow protocol=TCP localport=%PGPORT% remoteip=localsubnet >>"%LOG%" 2>&1
sc query %SERVICE% | find "RUNNING" >nul
if errorlevel 1 (
    net start %SERVICE% >>"%LOG%" 2>&1
    if errorlevel 1 exit /b 7
)
call :wait
exit /b %ERRORLEVEL%

:database
set "DATA=%~3"
call :wait
if errorlevel 1 exit /b 8
set /p PGPASSWORD=<"%~4"
"%BIN%\psql.exe" -X -q -h localhost -U postgres -d postgres -v ON_ERROR_STOP=1 -f "%~5" >>"%LOG%" 2>&1
if errorlevel 1 exit /b 9
>"%DATA%\aguara-lista.txt" echo Usuario y base de datos de AguaraPOS creados.
exit /b 0

:remove
net stop %SERVICE% >>"%LOG%" 2>&1
"%BIN%\pg_ctl.exe" unregister -N %SERVICE% >>"%LOG%" 2>&1
netsh advfirewall firewall delete rule name="%RULE%" >>"%LOG%" 2>&1
exit /b 0

:wait
for /l %%i in (1,1,30) do (
    "%BIN%\pg_isready.exe" -q -h localhost -p %PGPORT%
    if not errorlevel 1 exit /b 0
    ping -n 2 127.0.0.1 >nul
)
>>"%LOG%" echo The server did not answer.
exit /b 1
