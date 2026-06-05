param(
    [string]$RepoOwner = "linuup",
    [string]$RepoName = "LinDesk_Update",
    [string]$Channel = "stable",
    [string]$Version = "",
    [string]$Notes = "",
    [string]$MinSupportedVersion = "1.0.0",
    [string]$InnoSetupDir = "D:\Inno Setup 6",
    [string]$IssPath = "",
    [string]$InstallerDir = "",
    [string]$OutputPath = "",
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

function Wait-BeforeExit {
    if ($NoPause) {
        return
    }
    Write-Host ""
    Read-Host "Press Enter to close this window"
}

trap {
    Write-Host ""
    Write-Host "Failed to build installer or generate update metadata." -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Wait-BeforeExit
    exit 1
}

function Read-CMakeValue {
    param(
        [string]$Content,
        [string]$Name
    )

    $pattern = "set\(\s*$Name\s+`"?([^`"\)\s]+)`"?"
    $match = [regex]::Match($Content, $pattern)
    if (-not $match.Success) {
        throw "Cannot find $Name in CMakeLists.txt."
    }
    return $match.Groups[1].Value.Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$cmakePath = Join-Path $repoRoot "CMakeLists.txt"
if (-not (Test-Path $cmakePath)) {
    throw "CMakeLists.txt was not found at $cmakePath."
}

# Read CMake value to get current version
$cmakeContent = Get-Content -LiteralPath $cmakePath -Raw
$major = Read-CMakeValue -Content $cmakeContent -Name "LINDESK_VERSION_MAJOR"
$minor = Read-CMakeValue -Content $cmakeContent -Name "LINDESK_VERSION_MINOR"
$patch = Read-CMakeValue -Content $cmakeContent -Name "LINDESK_VERSION_PATCH"
$currentVersion = "$major.$minor.$patch"

# Check if running interactively
$isInteractive = $true
if ($Version -or $NoPause) {
    $isInteractive = $false
}

$version = $Version
$notes = $Notes

if ($isInteractive) {
    Write-Host "=== LinDesk Packaging Assistant ===" -ForegroundColor Cyan
    Write-Host "Current version in CMakeLists.txt is: $currentVersion" -ForegroundColor Yellow
    
    $inputVersion = Read-Host "Enter new version number (e.g. 1.0.8, press Enter to keep current)"
    if ([string]::IsNullOrWhiteSpace($inputVersion)) {
        $version = $currentVersion
    } else {
        if ($inputVersion -notmatch '^\d+\.\d+\.\d+$') {
            throw "Invalid version format. Expected X.Y.Z (e.g. 1.0.8)"
        }
        $version = $inputVersion
    }

    $inputNotes = Read-Host "Enter release description / update notes (press Enter for default)"
    if ([string]::IsNullOrWhiteSpace($inputNotes)) {
        $notes = "LinDesk update."
    } else {
        $notes = $inputNotes
    }
} else {
    if ([string]::IsNullOrEmpty($version)) {
        $version = $currentVersion
    }
    if ([string]::IsNullOrEmpty($notes)) {
        $notes = "LinDesk update."
    }
}

# If version changed, update CMakeLists.txt and reconfigure
$versionChanged = $version -ne $currentVersion
if ($versionChanged) {
    Write-Host "Updating version to $version in CMakeLists.txt..." -ForegroundColor Green
    $versionParts = $version -split '\.'
    $newMajor = $versionParts[0]
    $newMinor = $versionParts[1]
    $newPatch = $versionParts[2]

    $cmakeContent = $cmakeContent -replace 'set\(\s*LINDESK_VERSION_MAJOR\s+\d+\s*\)', "set(LINDESK_VERSION_MAJOR $newMajor)"
    $cmakeContent = $cmakeContent -replace 'set\(\s*LINDESK_VERSION_MINOR\s+\d+\s*\)', "set(LINDESK_VERSION_MINOR $newMinor)"
    $cmakeContent = $cmakeContent -replace 'set\(\s*LINDESK_VERSION_PATCH\s+\d+\s*\)', "set(LINDESK_VERSION_PATCH $newPatch)"
    Set-Content -LiteralPath $cmakePath -Value $cmakeContent -Force

    Write-Host "Reconfiguring CMake..." -ForegroundColor Green
    & cmake -S $repoRoot -B (Join-Path $repoRoot "build/build_64")
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed."
    }
}

# Compile Release configuration
$shouldBuild = $true
if ($isInteractive) {
    $buildOption = Read-Host "Compile project in Release mode before packaging? [Y/n]"
    if ([string]::IsNullOrWhiteSpace($buildOption) -or $buildOption.Trim().ToLower() -eq 'y' -or $buildOption.Trim().ToLower() -eq 'yes') {
        $shouldBuild = $true
    } else {
        $shouldBuild = $false
    }
}

if ($shouldBuild) {
    Write-Host "Building LinDesk in Release mode..." -ForegroundColor Green
    & cmake --build (Join-Path $repoRoot "build/build_64") --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build LinDesk in Release mode."
    }
}

if ([string]::IsNullOrWhiteSpace($InstallerDir)) {
    $InstallerDir = Join-Path $repoRoot "installer"
}

if ([string]::IsNullOrWhiteSpace($IssPath)) {
    $IssPath = Join-Path $InstallerDir "package.iss"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $InstallerDir "update.json"
}

$isccPath = Join-Path $InnoSetupDir "ISCC.exe"
if (-not (Test-Path $isccPath)) {
    throw "ISCC.exe was not found: $isccPath"
}

if (-not (Test-Path $IssPath)) {
    throw "Inno Setup script was not found: $IssPath"
}

Write-Host "Building installer with Inno Setup..." -ForegroundColor Green
Write-Host "  ISCC: $isccPath"
Write-Host "  ISS: $IssPath"
& $isccPath $IssPath
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compiler failed with exit code $LASTEXITCODE."
}

$installerName = "LinDesk_Setup_$version.exe"
$installerPath = Join-Path $InstallerDir $installerName
if (-not (Test-Path $installerPath)) {
    throw "Installer was not found: $installerPath"
}

$hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
$publishedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$tag = "v$version"
$url = "https://github.com/$RepoOwner/$RepoName/releases/download/$tag/$installerName"

$metadata = [ordered]@{
    version = $version
    channel = $Channel
    url = $url
    sha256 = $hash
    notes = $notes
    publishedAt = $publishedAt
    minSupportedVersion = $MinSupportedVersion
}

$json = $metadata | ConvertTo-Json -Depth 4
$outputDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

if (Test-Path $OutputPath) {
    Write-Host "Existing update metadata found. Overwriting: $OutputPath" -ForegroundColor Yellow
    Remove-Item -LiteralPath $OutputPath -Force
}

try {
    Set-Content -LiteralPath $OutputPath -Value $json -Encoding UTF8 -Force

    Write-Host ""
    Write-Host "Success: built installer and generated update metadata." -ForegroundColor Green
    Write-Host "  Version: $version"
    Write-Host "  ISS: $IssPath"
    Write-Host "  Installer: $installerPath"
    Write-Host "  SHA256: $hash"
    Write-Host "  URL: $url"
    Write-Host "  Output: $OutputPath"
    Wait-BeforeExit
    exit 0
} catch {
    Write-Host ""
    Write-Host "Failed to generate update metadata." -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Wait-BeforeExit
    exit 1
}
