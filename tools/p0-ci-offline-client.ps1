[CmdletBinding()]
param([string]$RoslynDirectory = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $RoslynDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio discovery unavailable' }
    $install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $install) { throw 'MSBuild installation not found' }
    $RoslynDirectory = Join-Path $install 'MSBuild/Current/Bin/Roslyn'
}
if (-not (Test-Path -LiteralPath (Join-Path $RoslynDirectory 'csc.exe'))) { throw 'Roslyn compiler unavailable' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools/client-baseline-check.ps1') -Mode Worktree -RoslynDirectory $RoslynDirectory
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$report = Get-Content -Raw -LiteralPath (Join-Path $root 'build/scratch/client-baseline/Worktree/results.json') | ConvertFrom-Json
if (@($report.tests).Count -ne 2) { throw 'Offline client must execute both targeted production checks' }
foreach ($test in $report.tests) { if (-not $test.passed) { throw "Offline client failed: $($test.name): $($test.error)" } }
Write-Output 'OFFLINE CLIENT PASS: compiled production slices and two checks; not Unity acceptance'
exit 0
