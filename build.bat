@echo off
setlocal enableextensions

rem ---------------------------------------------------------------
rem MultiMonitorPanel ? build script
rem ---------------------------------------------------------------

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found at: "%VCVARS%"
    echo         Install Visual Studio 2022 Build Tools with the C++ workload.
    exit /b 1
)

rem vcvars calls vswhere.exe (in the VS Installer dir), which isn't on PATH by
rem default — add it so vcvars doesn't emit a "'vswhere.exe' is not recognized"
rem error on stderr (the build worked anyway, but the message was noise).
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if exist "%VSWHERE_DIR%\vswhere.exe" set "PATH=%VSWHERE_DIR%;%PATH%"

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed to initialize
    exit /b 1
)

if not exist "%ROOT%\build" mkdir "%ROOT%\build"

rem Clean old artifacts.
if exist "%ROOT%\build\*.obj" del /q "%ROOT%\build\*.obj"
if exist "%ROOT%\build\*.res" del /q "%ROOT%\build\*.res"

rem Compile resources.
rc /nologo /fo "%ROOT%\build\panel.res" "%ROOT%\res\panel.rc"
if errorlevel 1 (
    echo [ERROR] rc.exe failed
    exit /b 1
)

rem Compile + link.
cl /nologo /std:c++17 /W3 /EHsc /O2 /utf-8 /permissive- ^
   /Fo"%ROOT%\build\\" /Fe"%ROOT%\build\panel.exe" ^
   "%ROOT%\src\panel.cpp" "%ROOT%\build\panel.res" ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   user32.lib shell32.lib shlwapi.lib comctl32.lib gdi32.lib ole32.lib shcore.lib advapi32.lib
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

rem Config now lives in the registry (HKCU\Software\MultiMonitorPanel), seeded
rem on first run — nothing to copy next to the exe.

rem Clean intermediate link artifacts — only panel.exe + ico are needed.
if exist "%ROOT%\build\panel.obj" del /q "%ROOT%\build\panel.obj"
if exist "%ROOT%\build\panel.res" del /q "%ROOT%\build\panel.res"

echo.
echo [OK] Built: %ROOT%\build\panel.exe
echo      Config: HKCU\Software\MultiMonitorPanel (registry, seeded on first run)
endlocal
