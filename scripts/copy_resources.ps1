param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [Parameter(Mandatory = $true)]
    [string]$Target
)

$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path -LiteralPath $Source).Path
$targetRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Target)
New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null

$skipNames = @('.webview2', '.git', 'node_modules', 'cache', 'tmp', 'temp')
$items = Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force
foreach ($item in $items) {
    $relative = $item.FullName.Substring($sourceRoot.Length).TrimStart('\', '/')
    $parts = $relative -split '[\\/]'
    $shouldSkip = $false
    foreach ($part in $parts) {
        if ($skipNames -contains $part) {
            $shouldSkip = $true
            break
        }
    }
    if ($shouldSkip) {
        continue
    }

    $destination = Join-Path $targetRoot $relative
    if ($item.PSIsContainer) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        continue
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $item.FullName -Destination $destination -Force
}
