# FastLook - Nuitka Optimized Build Script
# Run from: c:\Users\a\Desktop\LinDesk\FastLook\
# Output:   .\dist\FastLook.exe

$ErrorActionPreference = "Stop"

Write-Host "=== FastLook Nuitka Build ===" -ForegroundColor Cyan
Write-Host "Starting optimized compilation..." -ForegroundColor Yellow

python -m nuitka `
    --standalone `
    --onefile `
    --windows-console-mode=disable `
    --windows-icon-from-ico=app_icon.png `
    --output-dir=build-nuitka `
    --output-filename=FastLook `
    --enable-plugin=pyside6 `
    --include-data-files=style.css=style.css `
    --include-data-files=app_icon.png=app_icon.png `
    --include-data-dir=office=office `
    --follow-imports `
    --assume-yes-for-downloads `
    --lto=no `
    --jobs=4 `
    main.py

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Build Successful! ===" -ForegroundColor Green
    Write-Host "Output: .\dist\FastLook.exe" -ForegroundColor Green
} else {
    Write-Host "`n=== Build Failed (exit code $LASTEXITCODE) ===" -ForegroundColor Red
    exit $LASTEXITCODE
}
