@echo off
rem Simple build.bat wrapper for Selaco Archipelago integration
rem This redirects to the main build script

echo Redirecting to Selaco Archipelago build script...
echo.

rem Check if the main build script exists
if not exist "build_selaco_archipelago.bat" (
    echo Error: build_selaco_archipelago.bat not found!
    echo Please make sure you're in the correct directory.
    pause
    exit /b 1
)

rem Call the main build script with all arguments
call build_selaco_archipelago.bat %*

exit /b %errorlevel%