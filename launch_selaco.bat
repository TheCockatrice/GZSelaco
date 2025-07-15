@echo off
echo Starting Selaco with Archipelago integration...
echo.

cd /d "C:\Users\Skuldier\Documents\Selaco_cursor\GZSelacoAP"

REM Set environment variables
set SELACO_CONFIG_PATH=C:\Users\Skuldier\Documents\Selaco_cursor\GZSelacoAP
set ARCHIPELAGO_CONFIG_PATH=C:\Users\Skuldier\Documents\Selaco_cursor\GZSelacoAP\archipelago.cfg

REM Launch Selaco
if exist "build\src\RelWithDebInfo\selaco.exe" (
    echo Launching Selaco...
    "build\src\RelWithDebInfo\selaco.exe" %*
) else if exist "build\src\selaco.exe" (
    echo Launching Selaco...
    "build\src\selaco.exe" %*
) else (
    echo ERROR: Selaco executable not found!
    echo You need to build the project first.
    echo Open the Visual Studio solution in the build directory and build it.
    pause
    exit /b 1
)
