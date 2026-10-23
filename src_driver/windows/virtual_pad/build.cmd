@echo off
rem Builds streamlight_virtual_pad.sys with the installed WDK and MSVC (no Visual Studio project).
rem Output: build\streamlight_virtual_pad.sys. The driver is not signed here; see README.md.

setlocal
set "MSVC=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
set "WDK=%ProgramFiles(x86)%\Windows Kits\10"
set "SDK=10.0.26100.0"
set "KMDF=1.33"

rem vcvars64.bat is not used: it fails on some PATH entries, so the compiler directory is added directly.
if not exist "%MSVC%\cl.exe" (
    echo MSVC compiler not found: "%MSVC%"
    exit /b 1
)
set "PATH=%MSVC%;%PATH%"

set "HERE=%~dp0"
set "OUT=%HERE%build"
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /kernel /c /W4 /WX /wd4324 /GS- /Zp8 /Gy ^
    /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000000 /D_WIN32_WINNT=0x0A00 ^
    /DKMDF_VERSION_MAJOR=1 /DKMDF_VERSION_MINOR=33 ^
    /I"%WDK%\Include\%SDK%\km" ^
    /I"%WDK%\Include\%SDK%\km\crt" ^
    /I"%WDK%\Include\%SDK%\shared" ^
    /I"%WDK%\Include\wdf\kmdf\%KMDF%" ^
    /Fo"%OUT%\driver.obj" "%HERE%driver.c" || exit /b 1

cl /nologo /kernel /c /W4 /WX /GS- /Zp8 /Gy ^
    /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000000 /D_WIN32_WINNT=0x0A00 ^
    /I"%WDK%\Include\%SDK%\km" ^
    /I"%WDK%\Include\%SDK%\km\crt" ^
    /I"%WDK%\Include\%SDK%\shared" ^
    /Fo"%OUT%\security_cookie.obj" "%HERE%security_cookie.c" || exit /b 1

link /nologo /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:FxDriverEntry /NODEFAULTLIB ^
    /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE /OPT:REF /INCREMENTAL:NO ^
    /LIBPATH:"%WDK%\Lib\%SDK%\km\x64" ^
    /LIBPATH:"%WDK%\Lib\wdf\kmdf\x64\%KMDF%" ^
    /OUT:"%OUT%\streamlight_virtual_pad.sys" "%OUT%\driver.obj" "%OUT%\security_cookie.obj" ^
    ntoskrnl.lib hal.lib wdmsec.lib wdfldr.lib wdfdriverentry.lib vhfkm.lib || exit /b 1

echo Built "%OUT%\streamlight_virtual_pad.sys"
endlocal
