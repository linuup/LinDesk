<#
.SYNOPSIS
    Packages the LinDesk Widget Runtime into a standalone zip archive for GitHub Releases.

.PARAMETER Version
    The runtime version to stamp into manifest.json and the zip filename (e.g., 1.0.0).

.PARAMETER OutputDir
    Output directory where the zip archive and digest file will be placed.

.PARAMETER BuildDir
    Build directory containing the compiled LinWidgetRuntime.dll and deployed runtime/linwidget.
#>

param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = "$PSScriptRoot/../dist",
    [string]$BuildDir = "$PSScriptRoot/../build/Release"
)

$ErrorActionPreference = "Stop"

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Packaging LinDesk Widget Runtime v$Version" -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

$runtimeSourceDir = Join-Path $BuildDir "runtime/linwidget"
if (-not (Test-Path $runtimeSourceDir)) {
    # Fallback to direct build dir
    $runtimeSourceDir = $BuildDir
}

$cefDllPath = Join-Path $runtimeSourceDir "libcef.dll"
if (-not (Test-Path $cefDllPath)) {
    throw "Cannot find libcef.dll in '$runtimeSourceDir'. Please build target LinWidgetRuntime first."
}

$stagingDir = Join-Path $env:TEMP ("linwidget_runtime_staging_" + [guid]::NewGuid().ToString("N"))
if (Test-Path $stagingDir) {
    Remove-Item -Path $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

try {
    Write-Host "Staging CEF runtime files from $runtimeSourceDir..." -ForegroundColor Yellow
    Copy-Item -Path (Join-Path $runtimeSourceDir "*") -Destination $stagingDir -Recurse -Force -Exclude @("LinWidgetRuntime.dll", "LinDeskSubprocess.exe", "*.lib", "*.exp", "*.pdb")

    # Write manifest.json
    $manifest = [ordered]@{
        name = "LinWidget Runtime"
        runtimeVersion = $Version
        protocolVersion = "1.0.0"
        qtVersion = "5.15.2"
        abi = "msvc2019_x64"
        builtAt = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssZ")
        releaseNotes = "LinDesk LinWidget Runtime release v$Version"
    }

    $manifestPath = Join-Path $stagingDir "manifest.json"
    $manifestJson = $manifest | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson, [System.Text.Encoding]::UTF8)

    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }

    $zipFileName = "linwidget-runtime-v$Version.zip"
    $zipFilePath = Join-Path $OutputDir $zipFileName
    if (Test-Path $zipFilePath) {
        Remove-Item -Path $zipFilePath -Force
    }

    Write-Host "Compressing archive to $zipFilePath..." -ForegroundColor Yellow
    Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipFilePath -CompressionLevel Optimal

    $hash = (Get-FileHash -Path $zipFilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $shaPath = $zipFilePath + ".sha256"
    [System.IO.File]::WriteAllText($shaPath, "$hash *$zipFileName`n", [System.Text.Encoding]::UTF8)

    Write-Host "`nSuccessfully packaged runtime:" -ForegroundColor Green
    Write-Host "Archive: $zipFilePath" -ForegroundColor Green
    Write-Host "SHA-256: $hash" -ForegroundColor Green
}
finally {
    if (Test-Path $stagingDir) {
        Remove-Item -Path $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
