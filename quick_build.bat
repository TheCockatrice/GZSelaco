@echo off
echo.
echo ===============================================
echo    Selaco Archipelago - Quick Build Script
echo ===============================================
echo.

REM Change to the script directory
cd /d "%~dp0"

REM Step 1: Quick setup (skip dependencies)
echo [1/2] Running quick setup...
echo.
python build_selaco_archipelago.py --skip-checks --quick
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Quick setup failed
    echo Try running the full build script: build_and_compile.bat
    pause
    exit /b 1
)

echo.
echo ===============================================
echo    Quick setup complete! Building...
echo ===============================================
echo.

REM Step 2: Build with MSBuild
echo [2/2] Building project...
echo.

REM Find MSBuild
set "MSBUILD_PATH="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
) else (
    set "MSBUILD_PATH=MSBuild.exe"
)

REM Build Release configuration
if exist "build\gzdoom.sln" (
    "%MSBUILD_PATH%" "build\gzdoom.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
) else (
    echo ERROR: Solution file not found. Run full build script first.
    pause
    exit /b 1
)

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ===============================================
echo         QUICK BUILD COMPLETED!
echo ===============================================
echo.
echo Executable should be in build\src\Release\
echo Run launch_selaco.bat to start the game
echo.
pause