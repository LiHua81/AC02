@echo off
setlocal enabledelayedexpansion

REM STM32 Flash Script using STM32CubeProgrammer CLI
REM Usage: Double-click or run: flash_stm32.bat [Debug|Release]

set PROGRAMMER=H:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
set PROJECT=D:\0000\111\STM32\Source\AC02

REM Default to Debug
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug

set ELF=%PROJECT%\build\%BUILD_TYPE%\AC02.elf

echo ========================================
echo STM32 Flash Script
echo ========================================
echo.

REM Find ELF file in all possible locations
if exist "%ELF%" goto :flash

REM Try alternative locations
set ALT_ELF=%PROJECT%\cmake-build-release-stm32_cubeclt_gnu-1\AC02.elf
if exist "%ALT_ELF%" (
    set ELF=%ALT_ELF%
    goto :flash
)

set ALT_ELF=%PROJECT%\cmake-build-release-mingw\AC02.elf
if exist "%ALT_ELF%" (
    set ELF=%ALT_ELF%
    goto :flash
)

REM ELF not found - auto-build first
echo [WARN] ELF not found at %ELF%
echo        Trying to auto-build first...
echo.
call "%PROJECT%\build_stm32.bat" ^
    2>nul
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Auto-build failed. Please build manually first.
    pause
    exit /b 1
)
REM After build_stm32.bat, elf is in cmake-build-release-stm32_cubeclt_gnu-1
set ELF=%PROJECT%\cmake-build-release-stm32_cubeclt_gnu-1\AC02.elf

:flash
echo ELF: %ELF%
echo.

if not exist "%ELF%" (
    echo [ERROR] ELF still not found: %ELF%
    echo        Please run cmake build first.
    pause
    exit /b 1
)

echo Flashing...
"%PROGRAMMER%" -c port=swd freq=4000 mode=UR -d "%ELF%" -v -rst

if errorlevel 1 (
    echo.
    echo [ERROR] Flash failed!
    echo Check ST-Link connection.
) else (
    echo.
    echo Flash completed successfully!
)

pause
