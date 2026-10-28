<#
.SYNOPSIS
    LinDesk Fast Context Menu - Master E2E Test Suite Runner.
.DESCRIPTION
    Executes the opaque-box, requirement-driven 4-tier E2E test suite for the LinDesk fast context menu pipeline:
    - CTest Unit Tests (8 core unit tests)
    - Diagnostic Latency Verification (parsing desktop.log for sub-150ms invariants)
    - Menu Exclusion Store Verification (Menuexclusions.json / Menuexclusions.dat)
    - Tier 1: Feature Coverage (F1 to F14, 70 test cases)
    - Tier 2: Boundary & Corner Cases (10 test cases)
    - Tier 3: Cross-Feature Combinations (8 test cases)
    - Tier 4: Real-World Scenarios (6 test cases)

.PARAMETER Tier
    Which tier to run: 'All' (default), '1', '2', '3', '4', 'Latency', 'Exclusions', 'CTest'.

.PARAMETER SkipCTest
    Skips the execution of CTest unit tests.

.PARAMETER LogPath
    Path to desktop.log for latency verification.

.PARAMETER ReportPath
    Path where the generated Markdown test report should be written.
#>

[CmdletBinding()]
param (
    [ValidateSet('All', '1', '2', '3', '4', 'Latency', 'Exclusions', 'CTest')]
    [string]$Tier = 'All',
    [switch]$SkipCTest,
    [string]$LogPath = "$env:LOCALAPPDATA\LinDesk\logs\desktop.log",
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Continue"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-Path (Join-Path $scriptDir "..\..")

Write-Host "==========================================================" -ForegroundColor Magenta
Write-Host " LinDesk Fast Context Menu - Master E2E Test Runner" -ForegroundColor Magenta
Write-Host " Project Root: $projectRoot" -ForegroundColor Gray
Write-Host " Mode: Tier $Tier | SkipCTest: $SkipCTest" -ForegroundColor Gray
Write-Host "==========================================================" -ForegroundColor Magenta

$results = [System.Collections.Generic.List[PSCustomObject]]::new()
$startTime = [System.DateTime]::UtcNow

function Run-Step([string]$StepName, [string]$Command, [string[]]$Arguments) {
    Write-Host "`n>>> Running: $StepName..." -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    
    $proc = Start-Process -FilePath $Command -ArgumentList $Arguments -NoNewWindow -PassThru -Wait
    $sw.Stop()

    $passed = ($proc.ExitCode -eq 0)
    $color = if ($passed) { "Green" } else { "Red" }
    Write-Host ">>> Finished: $StepName - Exit Code: $($proc.ExitCode) ($($sw.ElapsedMilliseconds)ms)" -ForegroundColor $color

    $results.Add([PSCustomObject]@{
        Step = $StepName
        Passed = $passed
        ExitCode = $proc.ExitCode
        DurationMs = $sw.ElapsedMilliseconds
    })
}

# 1. CTest Unit Tests
if (-not $SkipCTest -and ($Tier -eq 'All' -or $Tier -eq 'CTest')) {
    Run-Step "CTest Unit Tests" "ctest.exe" @("--test-dir", (Join-Path $projectRoot "build"), "-C", "Debug", "--output-on-failure")
}

# 2. Context Menu Latency Verification
if ($Tier -eq 'All' -or $Tier -eq 'Latency') {
    $latencyScript = Join-Path $scriptDir "test_context_menu_latency.ps1"
    Run-Step "Diagnostic Latency Verification (desktop.log)" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $latencyScript, "-LogPath", $LogPath)
}

# 3. Menu Exclusions Store Verification
if ($Tier -eq 'All' -or $Tier -eq 'Exclusions') {
    $exclusionsScript = Join-Path $scriptDir "test_menu_exclusions.ps1"
    Run-Step "ContextMenu Filter Store Verification" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $exclusionsScript)
}

# 4. Tier 1: Feature Coverage (F1 to F14)
if ($Tier -eq 'All' -or $Tier -eq '1') {
    $tier1Script = Join-Path $scriptDir "test_tier1_features.ps1"
    Run-Step "Tier 1: Feature Coverage (F1 to F14)" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $tier1Script)
}

# 5. Tier 2: Boundary & Corner Cases
if ($Tier -eq 'All' -or $Tier -eq '2') {
    $tier2Script = Join-Path $scriptDir "test_tier2_boundaries.ps1"
    Run-Step "Tier 2: Boundary & Corner Cases" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $tier2Script)
}

# 6. Tier 3: Cross-Feature Combinations
if ($Tier -eq 'All' -or $Tier -eq '3') {
    $tier3Script = Join-Path $scriptDir "test_tier3_combinations.ps1"
    Run-Step "Tier 3: Cross-Feature Combinations" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $tier3Script)
}

# 7. Tier 4: Real-World Scenarios & Benchmarks
if ($Tier -eq 'All' -or $Tier -eq '4') {
    $tier4Script = Join-Path $scriptDir "test_tier4_scenarios.ps1"
    Run-Step "Tier 4: Real-World Scenarios & 60 FPS" "powershell.exe" @("-ExecutionPolicy", "Bypass", "-File", $tier4Script)
}

$endTime = [System.DateTime]::UtcNow
$totalDuration = [math]::Round(($endTime - $startTime).TotalSeconds, 2)

# Summary table
Write-Host "`n==========================================================" -ForegroundColor Magenta
Write-Host " LinDesk Fast Context Menu - E2E Execution Summary" -ForegroundColor Magenta
Write-Host "==========================================================" -ForegroundColor Magenta

$passedCount = ($results | Where-Object { $_.Passed }).Count
$failedCount = ($results | Where-Object { -not $_.Passed }).Count

$results | Format-Table Step, Passed, ExitCode, @{Name="Duration (s)"; Expression={[math]::Round($_.DurationMs / 1000.0, 2)}} -AutoSize | Out-String | Write-Host

Write-Host "Total Steps Run: $($results.Count) | Passed: $passedCount | Failed: $failedCount" -ForegroundColor $(if ($failedCount -eq 0) { "Green" } else { "Yellow" })
Write-Host "Total Wall-Clock Time: ${totalDuration}s" -ForegroundColor Gray

# Optional Markdown Report Output
if (-not [string]::IsNullOrEmpty($ReportPath)) {
    $sb = [System.Text.StringBuilder]::new()
    $sb.AppendLine("# Fast Context Menu E2E Test Suite Execution Report") | Out-Null
    $sb.AppendLine("*Executed on: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss UTC')*`n") | Out-Null
    $sb.AppendLine("| Suite / Step | Status | Exit Code | Duration (s) |") | Out-Null
    $sb.AppendLine("|---|---|---|---|") | Out-Null
    foreach ($r in $results) {
        $statusIcon = if ($r.Passed) { "✅ PASS" } else { "❌ FAIL" }
        $sb.AppendLine("| $($r.Step) | $statusIcon | $($r.ExitCode) | $([math]::Round($r.DurationMs / 1000.0, 2)) |") | Out-Null
    }
    $sb.AppendLine("`n**Total Steps:** $($results.Count) | **Passed:** $passedCount | **Failed:** $failedCount") | Out-Null
    [System.IO.File]::WriteAllText($ReportPath, $sb.ToString(), [System.Text.Encoding]::UTF8)
    Write-Host "Report written to: $ReportPath" -ForegroundColor Cyan
}

if ($failedCount -gt 0) {
    # If CTest failed due to pre-existing baseline issue, note it
    Write-Host "`n[NOTE] Review failures above. Pre-existing CTest baseline environment issues (Tests 3 & 6) do not block context menu pipeline validation." -ForegroundColor Yellow
}

exit $(if ($failedCount -eq 0) { 0 } else { 1 })
