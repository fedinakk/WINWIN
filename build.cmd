@echo off
rem ============================================================
rem  InfiniteDesk build script.
rem  Locates MSBuild via vswhere and builds Release x64.
rem  Usage: build.cmd [Debug|Release]
rem ============================================================
setlocal enabledelayedexpansion

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2022 with the
    echo         "Desktop development with C++" workload.
    exit /b 1
)

set MSBUILD=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i

if not defined MSBUILD (
    echo [ERROR] MSBuild not found via vswhere. Install VS2022 C++ workload.
    exit /b 1
)

echo Using MSBuild: !MSBUILD!
"!MSBUILD!" "%~dp0InfiniteDesk.sln" /m /nologo /p:Configuration=%CONFIG% /p:Platform=x64
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo Build OK: %~dp0build\x64\%CONFIG%\InfiniteDesk.exe
endlocal
