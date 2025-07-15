@echo off
echo.
echo ===============================================
echo    Selaco Archipelago - Complete Build Script
echo ===============================================
echo.

REM Change to the script directory
cd /d "%~dp0"

REM Check if Python is available
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.7+ and add it to your PATH
    pause
    exit /b 1
)

REM Step 1: Run the Python setup script
echo [1/2] Running Python setup script...
echo.
python build_selaco_archipelago.py --skip-checks
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Python setup script failed
    echo Please check the output above for errors
    pause
    exit /b 1
)

echo.
echo ===============================================
echo    Setup complete! Starting compilation...
echo ===============================================
echo.

REM Step 2: Build the project using MSBuild
echo [2/2] Building project with MSBuild...
echo.

REM Try to find MSBuild in common locations
set "MSBUILD_PATH="

REM Check Visual Studio 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

REM Check Visual Studio 2019
if "%MSBUILD_PATH%"=="" (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )
)

REM Check if MSBuild was found
if "%MSBUILD_PATH%"=="" (
    echo WARNING: MSBuild not found in standard locations
    echo Trying to use MSBuild from PATH...
    set "MSBUILD_PATH=MSBuild.exe"
)

REM Find the solution file
if exist "build\gzdoom.sln" (
    set "SOLUTION_FILE=build\gzdoom.sln"
) else if exist "build\zdoom.sln" (
    set "SOLUTION_FILE=build\zdoom.sln"
) else (
    echo ERROR: Solution file not found in build directory
    echo Make sure the Python setup script completed successfully
    pause
    exit /b 1
)

REM Build the solution
echo Building solution: %SOLUTION_FILE%
echo Using MSBuild: %MSBUILD_PATH%
echo.

"%MSBUILD_PATH%" "%SOLUTION_FILE%" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed
    echo.
    echo Troubleshooting tips:
    echo 1. Make sure Visual Studio 2019 or 2022 is installed with C++ tools
    echo 2. Try opening the solution in Visual Studio and building manually
    echo 3. Check the build output above for specific error messages
    echo.
    pause
    exit /b 1
)

echo.
echo ===============================================
echo         BUILD COMPLETED SUCCESSFULLY!
echo ===============================================
echo.

REM Find the executable
set "EXE_PATH="
if exist "build\src\Release\selaco.exe" (
    set "EXE_PATH=build\src\Release\selaco.exe"
) else if exist "build\src\Release\gzdoom.exe" (
    set "EXE_PATH=build\src\Release\gzdoom.exe"
) else if exist "build\src\selaco.exe" (
    set "EXE_PATH=build\src\selaco.exe"
) else if exist "build\src\gzdoom.exe" (
    set "EXE_PATH=build\src\gzdoom.exe"
)

if "%EXE_PATH%"=="" (
    echo WARNING: Executable not found in expected locations
    echo Check the build\src directory for the executable
) else (
    echo Executable created: %EXE_PATH%
)

echo.
echo Configuration file: archipelago.cfg
echo Launch script: launch_selaco.bat
echo.
echo You can now:
echo 1. Run launch_selaco.bat to start the game
echo 2. Edit archipelago.cfg to configure your server settings
echo 3. Use console commands: ap_connect, ap_status, ap_items, ap_chat
echo.
echo Console Commands:
echo   ap_connect ^<server^> ^<port^> ^<slot^> [password]
echo   ap_status
echo   ap_items
echo   ap_chat ^<message^>
echo.

pause