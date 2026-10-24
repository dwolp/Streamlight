@echo off
setlocal enabledelayedexpansion

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

set SERVICE_NAME=StreamlightService
set "SERVICE_BIN=%ROOT_DIR%\tools\sunshinesvc.exe"
set "SERVICE_CONFIG_DIR=%LOCALAPPDATA%\SudoMaker\Streamlight"
set "SERVICE_CONFIG_FILE=%SERVICE_CONFIG_DIR%\service_start_type.txt"

rem Set service to demand start. It will be changed to auto later if the user selected that option.
set SERVICE_START_TYPE=demand

rem Remove the legacy SunshineSvc service
net stop sunshinesvc
sc delete sunshinesvc

rem Take over from Apollo: keep the start type of its service, then remove it so the two don't
rem compete for the same ports. A start type saved by a previous Streamlight install still wins.
sc qc ApolloService > nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "tokens=3" %%i in ('sc qc ApolloService ^| findstr /C:"START_TYPE"') do (
        set "APOLLO_START_TYPE=%%i"
    )
    echo Apollo service start type: [!APOLLO_START_TYPE!]

    if "!APOLLO_START_TYPE!"=="2" (
        sc qc ApolloService | findstr /C:"(DELAYED)" >nul
        if !ERRORLEVEL!==0 (
            set SERVICE_START_TYPE=delayed-auto
        ) else (
            set SERVICE_START_TYPE=auto
        )
    ) else if "!APOLLO_START_TYPE!"=="4" (
        set SERVICE_START_TYPE=disabled
    )

    net stop ApolloService
    sc delete ApolloService
)

rem Check if StreamlightService already exists
sc qc %SERVICE_NAME% > nul 2>&1
if %ERRORLEVEL%==0 (
    rem Stop the existing service if running
    net stop %SERVICE_NAME%

    rem Reconfigure the existing service
    set SC_CMD=config
) else (
    rem Create a new service
    set SC_CMD=create
)

rem Check if we have a saved start type from previous installation
if exist "%SERVICE_CONFIG_FILE%" (
    rem Debug output file content
    type "%SERVICE_CONFIG_FILE%"

    rem Read the saved start type
    for /f "usebackq delims=" %%a in ("%SERVICE_CONFIG_FILE%") do (
        set "SAVED_START_TYPE=%%a"
    )

    echo Raw saved start type: [!SAVED_START_TYPE!]

    rem Check start type
    if "!SAVED_START_TYPE!"=="2-delayed" (
        set SERVICE_START_TYPE=delayed-auto
    ) else if "!SAVED_START_TYPE!"=="2" (
        set SERVICE_START_TYPE=auto
    ) else if "!SAVED_START_TYPE!"=="3" (
        set SERVICE_START_TYPE=demand
    ) else if "!SAVED_START_TYPE!"=="4" (
        set SERVICE_START_TYPE=disabled
    )

    del "%SERVICE_CONFIG_FILE%"
)

echo Setting service start type set to: [!SERVICE_START_TYPE!]

rem Run the sc command to create/reconfigure the service
sc %SC_CMD% %SERVICE_NAME% binPath= "\"%SERVICE_BIN%\"" start= %SERVICE_START_TYPE% DisplayName= "Streamlight Service"

rem Set the description of the service
sc description %SERVICE_NAME% "Streamlight is a self-hosted game stream host for Moonlight."

rem Start the new service
net start %SERVICE_NAME%
