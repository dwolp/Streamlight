@echo off
rem Builds and runs the user-mode protocol tests with MSVC and the Windows SDK.

setlocal
set "VCROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\10.0.26100.0"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\10.0.26100.0"

if not exist "%VCROOT%\bin\Hostx64\x64\cl.exe" (
    echo MSVC compiler not found: "%VCROOT%"
    exit /b 1
)
set "PATH=%VCROOT%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%VCROOT%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared"
set "LIB=%VCROOT%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"

set "HERE=%~dp0"
set "OUT=%HERE%..\build\tests"
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /W4 /WX /Fo"%OUT%\\" /Fe"%OUT%\protocol_test.exe" ^
    "%HERE%protocol_test.c" "%HERE%..\switch_pro_protocol.c" || exit /b 1

rem Driver control tool; needs the installed driver, so it is built but not run here.
cl /nologo /W4 /WX /Fo"%OUT%\\" /Fe"%OUT%\pad_control.exe" ^
    "%HERE%pad_control.c" setupapi.lib || exit /b 1

"%OUT%\protocol_test.exe"
endlocal & exit /b %ERRORLEVEL%
