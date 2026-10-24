@echo off

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "OLD_DIR=%%~fI"

rem Create the config directory if it didn't already exist
set "NEW_DIR=%OLD_DIR%\config"
if not exist "%NEW_DIR%\" mkdir "%NEW_DIR%"
icacls "%NEW_DIR%" /reset

rem On first install, copy the configuration of an existing Apollo install, so the web UI login,
rem paired clients, apps and covers carry over. It is copied, not moved, so Apollo keeps working.
if exist "%NEW_DIR%\sunshine.conf" goto apollo_done
set "APOLLO_DIR="
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\SudoMaker\Apollo" /ve /reg:64 2^>nul ^| findstr /C:"REG_SZ"') do set "APOLLO_DIR=%%b"
if not defined APOLLO_DIR if defined ProgramW6432 set "APOLLO_DIR=%ProgramW6432%\Apollo"
if not defined APOLLO_DIR set "APOLLO_DIR=%ProgramFiles%\Apollo"
if /i "%APOLLO_DIR%"=="%OLD_DIR%" goto apollo_done
if not exist "%APOLLO_DIR%\config\sunshine.conf" goto apollo_done
echo Copying configuration from Apollo: %APOLLO_DIR%\config
robocopy "%APOLLO_DIR%\config" "%NEW_DIR%" /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS
icacls "%NEW_DIR%" /reset /T
:apollo_done

rem Migrate all files that aren't already present in the config dir
if exist "%OLD_DIR%\apps.json" (
    if not exist "%NEW_DIR%\apps.json" (
        move "%OLD_DIR%\apps.json" "%NEW_DIR%\apps.json"
        icacls "%NEW_DIR%\apps.json" /reset
    )
)
if exist "%OLD_DIR%\sunshine.conf" (
    if not exist "%NEW_DIR%\sunshine.conf" (
        move "%OLD_DIR%\sunshine.conf" "%NEW_DIR%\sunshine.conf"
        icacls "%NEW_DIR%\sunshine.conf" /reset
    )
)
if exist "%OLD_DIR%\sunshine_state.json" (
    if not exist "%NEW_DIR%\sunshine_state.json" (
        move "%OLD_DIR%\sunshine_state.json" "%NEW_DIR%\sunshine_state.json"
        icacls "%NEW_DIR%\sunshine_state.json" /reset
    )
)

rem Migrate the credentials directory
if exist "%OLD_DIR%\credentials\" (
    if not exist "%NEW_DIR%\credentials\" (
        move "%OLD_DIR%\credentials" "%NEW_DIR%\"
    )
)

rem Create the credentials directory if it wasn't migrated or already existing
if not exist "%NEW_DIR%\credentials\" mkdir "%NEW_DIR%\credentials"

rem Disallow read access to the credentials directory contents for normal users
rem Note: We must use the SIDs directly because "Users" and "Administrators" are localized
icacls "%NEW_DIR%\credentials" /inheritance:r
icacls "%NEW_DIR%\credentials" /grant:r *S-1-5-32-544:(OI)(CI)(F)
icacls "%NEW_DIR%\credentials" /grant:r *S-1-5-32-545:(R)

rem Migrate the covers directory
if exist "%OLD_DIR%\covers\" (
    if not exist "%NEW_DIR%\covers\" (
        move "%OLD_DIR%\covers" "%NEW_DIR%\"

        rem Fix apps.json image path values that point at the old covers directory
        powershell -NoProfile -c "(Get-Content '%NEW_DIR%\apps.json').replace('.\/covers\/', '.\/config\/covers\/') | Set-Content '%NEW_DIR%\apps.json'"
    )
)

rem Remove log files
del "%OLD_DIR%\*.txt"
del "%OLD_DIR%\*.log"
