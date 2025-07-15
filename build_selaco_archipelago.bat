@echo off
setlocal enabledelayedexpansion

rem **
rem ** build_selaco_archipelago.bat
rem ** Comprehensive build script for Selaco with Archipelago integration (Windows)
rem **
rem ** This script handles all dependencies, compilation, and setup for Selaco
rem ** with the Archipelago multiworld randomizer integration on Windows.
rem **
rem ** Prerequisites:
rem **   - Visual Studio 2019 or later (with C++ build tools)
rem **   - Git for Windows
rem **   - CMake (3.16 or later)
rem **
rem **---------------------------------------------------------------------------

rem Get script directory
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "ARCHIPELAGO_DIR=%SCRIPT_DIR%src\archipelago"

rem Build configuration
set "BUILD_TYPE=Release"
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

rem Parse command line arguments
:parse_args
if "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto parse_args
)
if "%~1"=="--release" (
    set "BUILD_TYPE=Release"
    shift
    goto parse_args
)
if "%~1"=="--relwithdebinfo" (
    set "BUILD_TYPE=RelWithDebInfo"
    shift
    goto parse_args
)
if "%~1"=="--jobs" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if "%~1"=="-j" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if "%~1"=="--clean" (
    set "CLEAN_BUILD=1"
    shift
    goto parse_args
)
if "%~1"=="--help" goto show_help
if "%~1"=="-h" goto show_help
if "%~1"=="/?" goto show_help
if not "%~1"=="" (
    echo Error: Unknown option: %~1
    goto show_help
)

goto main

:show_help
echo Usage: %~nx0 [OPTIONS]
echo Options:
echo   --debug              Build in Debug mode
echo   --release            Build in Release mode (default)
echo   --relwithdebinfo     Build with debug info
echo   --jobs / -j N        Use N parallel jobs (default: %JOBS%)
echo   --clean              Clean build directory first
echo   --help / -h / /?     Show this help
exit /b 0

:log_info
echo [INFO] %~1
exit /b 0

:log_success
echo [SUCCESS] %~1
exit /b 0

:log_warning
echo [WARNING] %~1
exit /b 0

:log_error
echo [ERROR] %~1
exit /b 1

:check_command
where "%~1" >nul 2>&1
exit /b %errorlevel%

:check_prerequisites
call :log_info "Checking prerequisites..."

rem Check essential tools
call :check_command git
if errorlevel 1 (
    call :log_error "Git not found. Please install Git for Windows."
    exit /b 1
)

call :check_command cmake
if errorlevel 1 (
    call :log_error "CMake not found. Please install CMake 3.16 or later."
    exit /b 1
)

rem Check for Visual Studio
call :check_command cl
if errorlevel 1 (
    call :log_error "Visual Studio C++ compiler not found. Please install Visual Studio 2019 or later with C++ build tools."
    exit /b 1
)

call :log_success "All prerequisites found"
exit /b 0

:clean_build
if defined CLEAN_BUILD (
    call :log_info "Cleaning build directory..."
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)
exit /b 0

:setup_build_dir
call :log_info "Setting up build directory..."
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
exit /b 0

:setup_vcpkg
call :log_info "Setting up vcpkg..."

if exist "vcpkg" (
    call :log_info "Updating vcpkg..."
    git -C vcpkg pull
) else (
    call :log_info "Cloning vcpkg..."
    git clone https://github.com/microsoft/vcpkg
)

rem Bootstrap vcpkg
if not exist "vcpkg\vcpkg.exe" (
    call :log_info "Bootstrapping vcpkg..."
    call vcpkg\bootstrap-vcpkg.bat
    if errorlevel 1 (
        call :log_error "Failed to bootstrap vcpkg"
        exit /b 1
    )
)

exit /b 0

:setup_zmusic
call :log_info "Setting up ZMusic..."

if exist "zmusic" (
    call :log_info "Updating ZMusic..."
    git -C zmusic fetch
) else (
    call :log_info "Cloning ZMusic..."
    git clone https://github.com/zdoom/zmusic
)

rem Checkout specific version
git -C zmusic checkout 1.1.12

rem Create build directory
if not exist "zmusic\build" mkdir "zmusic\build"
if not exist "vcpkg_installed" mkdir "vcpkg_installed"

call :log_info "Building ZMusic..."
cmake -S zmusic -B zmusic\build -DCMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_LIBSNDFILE=1 -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DVCPKG_INSTALLED_DIR=..\vcpkg_installed\

cmake --build zmusic\build --config %BUILD_TYPE% --parallel %JOBS%

if errorlevel 1 (
    call :log_error "Failed to build ZMusic"
    exit /b 1
)

call :log_success "ZMusic built successfully"
exit /b 0

:verify_archipelago
call :log_info "Verifying Archipelago integration..."

rem Check if Archipelago source files exist
set "ARCHIPELAGO_FILES=websocket_client.h websocket_client.cpp archipelago_client.h archipelago_client.cpp ap_definitions.h ap_definitions.cpp"

for %%f in (%ARCHIPELAGO_FILES%) do (
    if not exist "%ARCHIPELAGO_DIR%\%%f" (
        call :log_error "Missing Archipelago file: %%f"
        exit /b 1
    )
)

rem Check if ZScript integration exists
if not exist "%SCRIPT_DIR%wadsrc\static\zscript\archipelago.zs" (
    call :log_error "Missing Archipelago ZScript integration"
    exit /b 1
)

rem Check if CMakeLists.txt includes Archipelago sources
findstr /c:"archipelago/" "%SCRIPT_DIR%src\CMakeLists.txt" >nul
if errorlevel 1 (
    call :log_error "Archipelago sources not found in CMakeLists.txt"
    exit /b 1
)

call :log_success "Archipelago integration verified"
exit /b 0

:build_selaco
call :log_info "Configuring Selaco build..."

rem Configure with CMake
cmake -S .. -B . -DCMAKE_TOOLCHAIN_FILE=vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DVCPKG_INSTALLED_DIR=vcpkg_installed\ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if errorlevel 1 (
    call :log_error "CMake configuration failed"
    exit /b 1
)

call :log_info "Building Selaco with Archipelago integration..."
cmake --build . --config %BUILD_TYPE% --parallel %JOBS%

if errorlevel 1 (
    call :log_error "Build failed!"
    exit /b 1
)

call :log_success "Selaco built successfully!"
exit /b 0

:create_config_files
call :log_info "Creating configuration files..."

rem Create Archipelago configuration
echo # Archipelago Configuration for Selaco> "%BUILD_DIR%\archipelago.cfg"
echo # This file is automatically created and updated by the game>> "%BUILD_DIR%\archipelago.cfg"
echo.>> "%BUILD_DIR%\archipelago.cfg"
echo # Server connection settings>> "%BUILD_DIR%\archipelago.cfg"
echo server_address=archipelago.gg>> "%BUILD_DIR%\archipelago.cfg"
echo server_port=38281>> "%BUILD_DIR%\archipelago.cfg"
echo.>> "%BUILD_DIR%\archipelago.cfg"
echo # Player identification>> "%BUILD_DIR%\archipelago.cfg"
echo slot_name=>> "%BUILD_DIR%\archipelago.cfg"
echo password=>> "%BUILD_DIR%\archipelago.cfg"
echo.>> "%BUILD_DIR%\archipelago.cfg"
echo # Connection behavior>> "%BUILD_DIR%\archipelago.cfg"
echo auto_connect=false>> "%BUILD_DIR%\archipelago.cfg"
echo auto_reconnect=true>> "%BUILD_DIR%\archipelago.cfg"
echo reconnect_delay=3000>> "%BUILD_DIR%\archipelago.cfg"
echo.>> "%BUILD_DIR%\archipelago.cfg"
echo # UI settings>> "%BUILD_DIR%\archipelago.cfg"
echo show_notifications=true>> "%BUILD_DIR%\archipelago.cfg"
echo show_chat=true>> "%BUILD_DIR%\archipelago.cfg"
echo.>> "%BUILD_DIR%\archipelago.cfg"
echo # HUD display>> "%BUILD_DIR%\archipelago.cfg"
echo # Set this in console: set ap_show_hud true/false>> "%BUILD_DIR%\archipelago.cfg"

rem Create launch script
echo @echo off> "%BUILD_DIR%\launch_selaco.bat"
echo rem Launch script for Selaco with Archipelago integration>> "%BUILD_DIR%\launch_selaco.bat"
echo cd /d "%%~dp0">> "%BUILD_DIR%\launch_selaco.bat"
echo.>> "%BUILD_DIR%\launch_selaco.bat"
echo rem Launch Selaco>> "%BUILD_DIR%\launch_selaco.bat"
echo if exist gzdoom.exe (>> "%BUILD_DIR%\launch_selaco.bat"
echo     gzdoom.exe %%*>> "%BUILD_DIR%\launch_selaco.bat"
echo ^) else if exist selaco.exe (>> "%BUILD_DIR%\launch_selaco.bat"
echo     selaco.exe %%*>> "%BUILD_DIR%\launch_selaco.bat"
echo ^) else (>> "%BUILD_DIR%\launch_selaco.bat"
echo     echo Error: Selaco executable not found!>> "%BUILD_DIR%\launch_selaco.bat"
echo     pause>> "%BUILD_DIR%\launch_selaco.bat"
echo     exit /b 1>> "%BUILD_DIR%\launch_selaco.bat"
echo ^)>> "%BUILD_DIR%\launch_selaco.bat"

call :log_success "Configuration files created"
exit /b 0

:show_summary
call :log_info "Build Summary:"
echo   Build Type: %BUILD_TYPE%
echo   Platform: Windows
echo   Jobs Used: %JOBS%
echo   Build Directory: %BUILD_DIR%

if exist "%BUILD_DIR%\gzdoom.exe" (
    echo   Executable: %BUILD_DIR%\gzdoom.exe
) else if exist "%BUILD_DIR%\selaco.exe" (
    echo   Executable: %BUILD_DIR%\selaco.exe
)

if exist "%BUILD_DIR%\archipelago.cfg" (
    echo   Archipelago Config: %BUILD_DIR%\archipelago.cfg
)

if exist "%BUILD_DIR%\launch_selaco.bat" (
    echo   Launch Script: %BUILD_DIR%\launch_selaco.bat
)

echo.
call :log_success "Build completed successfully!"
echo.
echo To run Selaco with Archipelago integration:
echo   cd %BUILD_DIR%
echo   launch_selaco.bat
echo.
echo Archipelago console commands:
echo   ap_connect ^<server^> ^<port^> ^<slot_name^> [password]
echo   ap_status
echo   ap_items
echo   ap_chat ^<message^>
echo.

rem Open build directory on success
if exist "%BUILD_DIR%" explorer "%BUILD_DIR%"

exit /b 0

:main
cd /d "%SCRIPT_DIR%"

call :log_info "Starting Selaco with Archipelago integration build..."
call :log_info "Platform: Windows"
call :log_info "Build Type: %BUILD_TYPE%"
call :log_info "Jobs: %JOBS%"

call :check_prerequisites
if errorlevel 1 exit /b 1

call :clean_build
call :setup_build_dir
call :setup_vcpkg
if errorlevel 1 exit /b 1

call :setup_zmusic
if errorlevel 1 exit /b 1

call :verify_archipelago
if errorlevel 1 exit /b 1

call :build_selaco
if errorlevel 1 exit /b 1

call :create_config_files
call :show_summary

exit /b 0