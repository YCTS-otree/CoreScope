param([string]$OutputStem = 'etw-pmu-validation')
$ErrorActionPreference = 'Stop'
if ($OutputStem -notmatch '^[a-zA-Z0-9-]+$') { throw 'OutputStem must be a simple file name' }
Set-Location -LiteralPath (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)
$projectPath = (Get-Location).Path
$outputPath = Join-Path $projectPath "build\$OutputStem"
$started = $false
$success = $false
Start-Transcript -Path "$outputPath-run.txt" -Force
try {
    $status = wpr -status 2>&1 | Out-String
    Write-Output $status
    if ($LASTEXITCODE -ne 0 -or $status -notmatch 'WPR is not recording') {
        throw 'Cannot confirm WPR is idle; refusing to change an existing recording'
    }
    wpr -start "$projectPath\scripts\research\etw-pmu.wprp!Pmu" -filemode
    if ($LASTEXITCODE -ne 0) { throw "WPR start failed: $LASTEXITCODE" }
    $started = $true
    & "$projectPath\build\etw-pmu-check.exe" --load | Out-File "$outputPath-load.txt" -Encoding ascii
    if ($LASTEXITCODE -ne 0) { throw "Workload failed: $LASTEXITCODE" }
    Get-Content "$outputPath-load.txt"
} catch {
    $_ | Out-String | Write-Output
} finally {
    if ($started) {
        wpr -stop "$outputPath.etl"
        if ($LASTEXITCODE -eq 0) {
            & "$projectPath\build\etw-pmu-check.exe" "$outputPath.etl" "$outputPath-load.txt" | Out-File "$outputPath-analysis.txt" -Encoding ascii
            $success = $LASTEXITCODE -eq 0
            Get-Content "$outputPath-analysis.txt"
        }
    }
    wpr -status
    Stop-Transcript
    if ($success) { 'passed' | Set-Content "$outputPath-complete.txt" } else { 'failed' | Set-Content "$outputPath-complete.txt" }
}
if (-not $success) { exit 1 }
