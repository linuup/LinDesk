<#
.SYNOPSIS
    Downloads and extracts the prebuilt CEF (Chromium Embedded Framework) Windows x64 binary distribution.

.DESCRIPTION
    This script downloads a tested, stable CEF Windows x64 binary package from the official Spotify CDN
    or user-specified mirror and unpacks it into the LinDesk 3rdparty/cef directory.
    Windows 10/11 built-in tar.exe is used for fast extraction.

.PARAMETER CefVersion
    The exact CEF version tag. Default: 120.2.7+g8c10557+chromium-120.0.6099.234
.PARAMETER TargetDir
    The destination directory. Default: <repo-root>/3rdparty/cef
.PARAMETER Force
    Overwrites existing files if set.
#>

param(
    [string]$CefVersion = "120.2.7+g8c10557+chromium-120.0.6099.234",
    [string]$TargetDir = "",
    [switch]$Force = $false
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path "$scriptDir/.."
if ([string]::IsNullOrWhiteSpace($TargetDir)) {
    $TargetDir = Join-Path $repoRoot "3rdparty/cef"
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " LinDesk - CEF Windows x64 Dependency Downloader" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Target Directory : $TargetDir"
Write-Host "CEF Version      : $CefVersion"

$cefHeader = Join-Path $TargetDir "include/cef_app.h"
if ((Test-Path $cefHeader) -and -not $Force) {
    Write-Host "`n[OK] CEF SDK is already installed at: $TargetDir" -ForegroundColor Green
    Write-Host "To re-download, pass -Force parameter."
    exit 0
}

$packageName = "cef_binary_${CefVersion}_windows64"
$archiveName = "${packageName}.tar.bz2"
$downloadUrl = "https://cef-builds.spotifycdn.com/${archiveName}"

$tempDir = Join-Path $env:TEMP "lindesk_cef_download"
if (-not (Test-Path $tempDir)) {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
}

$archivePath = Join-Path $tempDir $archiveName

Write-Host "`n[1/3] Downloading CEF archive from: $downloadUrl" -ForegroundColor Yellow
Write-Host "      Saving to: $archivePath"

if (-not (Test-Path $archivePath) -or $Force) {
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $webClient = New-Object System.Net.WebClient
        $webClient.DownloadFile($downloadUrl, $archivePath)
        Write-Host "      Download completed successfully." -ForegroundColor Green
    } catch {
        Write-Host "`n[ERROR] Automated download failed: $_" -ForegroundColor Red
        Write-Host "`nYou can manually download the file from:" -ForegroundColor Yellow
        Write-Host "  $downloadUrl" -ForegroundColor White
        Write-Host "And unpack it to:" -ForegroundColor Yellow
        Write-Host "  $TargetDir" -ForegroundColor White
        exit 1
    }
} else {
    Write-Host "      Cached archive found at $archivePath, skipping download." -ForegroundColor Green
}

Write-Host "`n[2/3] Extracting archive..." -ForegroundColor Yellow
$extractTemp = Join-Path $tempDir "extracted"
if (Test-Path $extractTemp) {
    Remove-Item -Recurse -Force $extractTemp -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $extractTemp -Force | Out-Null

$tarExe = Get-Command "tar.exe" -ErrorAction SilentlyContinue
if (-not $tarExe) {
    Write-Host "[ERROR] Windows tar.exe not found. Please extract $archivePath manually to $TargetDir." -ForegroundColor Red
    exit 1
}

& $tarExe -xf $archivePath -C $extractTemp
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Extraction failed with exit code $LASTEXITCODE." -ForegroundColor Red
    exit 1
}

Write-Host "`n[3/3] Deploying to target directory: $TargetDir" -ForegroundColor Yellow
if (Test-Path $TargetDir) {
    Remove-Item -Recurse -Force $TargetDir -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path (Split-Path -Parent $TargetDir) -Force -ErrorAction SilentlyContinue | Out-Null

$extractedSubdir = Join-Path $extractTemp $packageName
if (Test-Path $extractedSubdir) {
    Move-Item -Path $extractedSubdir -Destination $TargetDir -Force
} else {
    Move-Item -Path "$extractTemp/*" -Destination $TargetDir -Force
}

# Cleanup temp extracted directory
Remove-Item -Recurse -Force $extractTemp -ErrorAction SilentlyContinue

if (Test-Path $cefHeader) {
    Write-Host "`n[SUCCESS] CEF SDK successfully installed to: $TargetDir" -ForegroundColor Green
    Write-Host "Ready to configure CMake and build LinDesk with CEF OSR support!" -ForegroundColor Cyan
} else {
    Write-Host "`n[WARNING] Extraction completed but $cefHeader not found. Please verify the folder structure." -ForegroundColor Yellow
}
