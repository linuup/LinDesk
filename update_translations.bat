@echo off
setlocal

cd /d "%~dp0"

set "LUPDATE_EXE="

if defined QT_BIN (
    if exist "%QT_BIN%\lupdate.exe" set "LUPDATE_EXE=%QT_BIN%\lupdate.exe"
)

if not defined LUPDATE_EXE if defined QTDIR (
    if exist "%QTDIR%\bin\lupdate.exe" set "LUPDATE_EXE=%QTDIR%\bin\lupdate.exe"
)

if not defined LUPDATE_EXE (
    where lupdate >nul 2>nul
    if not errorlevel 1 set "LUPDATE_EXE=lupdate"
)

if not defined LUPDATE_EXE (
    echo [ERROR] Cannot find lupdate.exe.
    echo.
    echo Try one of the following:
    echo   1. Add Qt bin directory to PATH
    echo   2. Set QT_BIN, for example:
    echo      set QT_BIN=C:\Qt\5.15.2\msvc2019_64\bin
    echo   3. Set QTDIR, for example:
    echo      set QTDIR=C:\Qt\5.15.2\msvc2019_64
    exit /b 1
)

echo [INFO] Using lupdate: %LUPDATE_EXE%
echo [INFO] Updating translation source files...

"%LUPDATE_EXE%" src -ts ^
    translations\lindesk_en_US.ts ^
    translations\lindesk_zh_CN.ts

if errorlevel 1 (
    echo [ERROR] lupdate failed.
    exit /b 1
)

echo [OK] Translation source files updated:
echo   translations\lindesk_en_US.ts
echo   translations\lindesk_zh_CN.ts

endlocal
