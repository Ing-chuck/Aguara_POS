<#
.SYNOPSIS
    Daily backup of the AguaraPOS database on the PostgreSQL server.

.DESCRIPTION
    Runs pg_dump (custom format: compressed, restorable with pg_restore) into
    -Destination as aguara-yyyyMMdd-HHmmss.dump, then deletes all but the
    newest -Keep copies. Works with PowerShell 2.0 (Windows 7) and later.

    The password is NOT passed here: pg_dump reads it from pgpass.conf of the
    Windows user that runs the task:
        %APPDATA%\postgresql\pgpass.conf     ->  localhost:5432:aguara:aguara:PASSWORD

    Schedule it (as that user), every day at 23:00:
        schtasks /Create /TN "AguaraPOS backup" /SC DAILY /ST 23:00 /TR ^
          "powershell -NoProfile -ExecutionPolicy Bypass -File C:\AguaraPOS\backup-postgresql.ps1 -Destination D:\AguaraBackups"

    Restore (stops nothing by itself: close AguaraPOS on every till first):
        pg_restore --clean --if-exists -h localhost -U aguara -d aguara D:\AguaraBackups\aguara-....dump

    Copy the backups to another disk or computer as well: a backup on the
    same disk is lost with that disk.

    (ASCII only on purpose: PowerShell 2.0-5.1 read scripts without a BOM
    in the system code page.)
#>
param(
    [string]$PgBin       = "",
    [string]$Database    = "aguara",
    [string]$User        = "aguara",
    [string]$Server      = "localhost",
    [int]   $Port        = 5432,
    [string]$Destination = "C:\AguaraBackups",
    [int]   $Keep        = 30
)

$ErrorActionPreference = "Stop"

# pg_dump: from -PgBin, else the newest PostgreSQL installed in Program Files, else PATH
if ($PgBin -eq "") {
    $installed = Get-ChildItem -Path "$env:ProgramFiles\PostgreSQL" -ErrorAction SilentlyContinue |
                 Where-Object { Test-Path (Join-Path $_.FullName "bin\pg_dump.exe") } |
                 Sort-Object { [double]($_.Name -replace '[^0-9.]', '') } -Descending |
                 Select-Object -First 1
    if ($installed) { $PgBin = Join-Path $installed.FullName "bin" }
}
$pgDump = "pg_dump.exe"
if ($PgBin -ne "") { $pgDump = Join-Path $PgBin "pg_dump.exe" }

if (-not (Test-Path $Destination)) {
    New-Item -ItemType Directory -Path $Destination | Out-Null
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$file  = Join-Path $Destination ("aguara-{0}.dump" -f $stamp)

& $pgDump "--host=$Server" "--port=$Port" "--username=$User" --no-password --format=custom "--file=$file" $Database
if ($LASTEXITCODE -ne 0) {
    if (Test-Path $file) { Remove-Item $file }
    Write-Error ("pg_dump failed (exit code {0}): no backup was made." -f $LASTEXITCODE)
    exit 1
}

# keep the newest $Keep copies (the names sort by date)
Get-ChildItem -Path $Destination -Filter "aguara-*.dump" |
    Sort-Object Name -Descending |
    Select-Object -Skip $Keep |
    Remove-Item

Write-Output ("Backup written: {0} ({1:N0} KB)" -f $file, ((Get-Item $file).Length / 1KB))
