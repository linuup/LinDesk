# FastLook - PyInstaller Build Script
# Run from: c:\Users\a\Desktop\LinDesk\FastLook\
# Output:   .\build-pyinstaller\FastLook.exe

$ErrorActionPreference = "Stop"

# Always resolve paths relative to this script's location
$SrcDir = $PSScriptRoot

Write-Host "=== FastLook PyInstaller Build ===" -ForegroundColor Cyan
Write-Host "Starting packaging..." -ForegroundColor Yellow

pyinstaller `
    --noconfirm `
    --onefile `
    --windowed `
    --icon="$SrcDir\app_icon.png" `
    --name=FastLook `
    --distpath="$SrcDir\build-pyinstaller" `
    --workpath="$SrcDir\build-pyinstaller\work" `
    --specpath="$SrcDir\build-pyinstaller" `
    --add-data="$SrcDir\style.css;." `
    --add-data="$SrcDir\app_icon.png;." `
    --hidden-import=win32com.client `
    --hidden-import=win32gui `
    --hidden-import=win32con `
    --hidden-import=uiautomation `
    --hidden-import=keyboard `
    --hidden-import=markdown `
    --hidden-import=PySide6.QtWebEngineWidgets `
    --hidden-import=PySide6.QtWebEngineCore `
    --collect-all=uiautomation `
    --collect-all=markdown `
    "$SrcDir\main.py"

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Build Successful! ===" -ForegroundColor Green
    Write-Host "Output: $SrcDir\build-pyinstaller\FastLook.exe" -ForegroundColor Green
} else {
    Write-Host "`n=== Build Failed (exit code $LASTEXITCODE) ===" -ForegroundColor Red
    exit $LASTEXITCODE
}
