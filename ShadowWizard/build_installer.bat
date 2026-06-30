@echo off
setlocal

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not exist "%ISCC%" (
    echo [ERROR] Inno Setup 6 is not installed or ISCC.exe was not found.
    echo Please download and install Inno Setup 6 from https://jrsoftware.org/isdl.php
    echo After installing, run this script again.
    pause
    exit /b 1
)

echo Building ShadowWizard_Setup.exe...
"%ISCC%" ShadowInstaller.iss

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Installer built successfully in the current folder!
) else (
    echo [ERROR] Failed to build the installer.
)

pause
