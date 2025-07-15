@echo off
echo.
echo ===============================================
echo    Selaco Archipelago - Debug Build Script
echo ===============================================
echo.

REM Change to the script directory
cd /d "%~dp0"

REM Step 1: Run setup with debug configuration
echo [1/2] Running debug setup...
echo.
python build_selaco_archipelago.py --skip-checks --build-type Debug
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Debug setup failed
    echo Please check the output above for errors
    pause
    exit /b 1
)

echo.
echo ===============================================
echo    Debug setup complete! Building...
echo ===============================================
echo.

REM Step 2: Build with MSBuild in Debug mode
echo [2/2] Building debug project...
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

REM Build Debug configuration
if exist "build\gzdoom.sln" (
    "%MSBUILD_PATH%" "build\gzdoom.sln" /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
) else (
    echo ERROR: Solution file not found. Run full build script first.
    pause
    exit /b 1
)

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Debug build failed
    pause
    exit /b 1
)

echo.
echo ===============================================
echo         DEBUG BUILD COMPLETED!
echo ===============================================
echo.

REM Find the debug executable
set "EXE_PATH="
if exist "build\src\Debug\selaco.exe" (
    set "EXE_PATH=build\src\Debug\selaco.exe"
) else if exist "build\src\Debug\gzdoom.exe" (
    set "EXE_PATH=build\src\Debug\gzdoom.exe"
)

if "%EXE_PATH%"=="" (
    echo WARNING: Debug executable not found in expected locations
    echo Check the build\src\Debug directory
) else (
    echo Debug executable created: %EXE_PATH%
)

echo.
echo Debug build includes:
echo - Full debug symbols (.pdb files)
echo - No optimization (easier debugging)
echo - Additional runtime checks
echo.
echo You can now debug with Visual Studio or your preferred debugger
echo.
pause