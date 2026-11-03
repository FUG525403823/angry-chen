# Serial, bounded P0 only. Invoke exclusively after the lead releases the engine/player lane.
[CmdletBinding()]
param([string]$Unity = $env:AC_UNITY, [string]$OutDir = 'build/reports/engine', [string]$Scratch = 'build/scratch/p0-engine')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $Unity -or -not (Test-Path -LiteralPath $Unity -PathType Leaf)) { throw 'Licensed AC_UNITY installation required' }
$env:AC_UNITY = $Unity
if (Test-Path -LiteralPath $Scratch) { throw 'Fresh scratch directory required; do not reuse prior build evidence' }
if (Test-Path -LiteralPath $OutDir) { throw 'Fresh evidence directory required' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
function Run([string]$File, [string[]]$Arguments = @()) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed exit=$LASTEXITCODE" }
}
function Native([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed exit=$LASTEXITCODE" }
}
$releaseProject = "$Scratch/release/client"
$devProject = "$Scratch/development/client"
Native node @('tools/p0-project.mjs', "out=$releaseProject")
Native node @('tools/p0-project.mjs', "out=$devProject")
$r = Get-Content -Raw "$releaseProject/p0-projection.json" | ConvertFrom-Json
$d = Get-Content -Raw "$devProject/p0-projection.json" | ConvertFrom-Json
if ($r.sourceFingerprint -ne $d.sourceFingerprint) { throw 'Source changed between build projections' }
$fingerprint = $r.sourceFingerprint
Copy-Item "$releaseProject/p0-projection.json" "$OutDir/release-projection.json"
Copy-Item "$devProject/p0-projection.json" "$OutDir/development-projection.json"
# The existing selftest entry resolves its own parent/client; nested client layout preserves it.
Run "$releaseProject/tools/selftest.ps1"
Run "$releaseProject/tools/playmode-smoke.ps1"
Run "$releaseProject/build.ps1" @('-Backend', 'mono')
. "$root/client/tools/lib/player-discovery.ps1"
$releasePlayer = Resolve-PublishedPlayer -Project $releaseProject -Player ''
if (-not $releasePlayer) { throw 'Release publication did not produce a player' }
# BuildEntry's real development switch; no source patch, and never relabel a release player.
$devOutput = [IO.Path]::GetFullPath("$Scratch/development/player")
$devLog = [IO.Path]::GetFullPath("$OutDir/development-build.log")
$child = Start-Process -FilePath $Unity -ArgumentList @('-batchmode', '-quit', '-nographics', '-projectPath', "`"$($d.projection)`"", '-executeMethod', 'Ac.Editor.BuildEntry.BuildWindows64', '-backend', 'mono', '-development', '1', '-output', "`"$devOutput`"", '-logFile', "`"$devLog`"") -PassThru
$null = $child.Handle
try {
    if (-not $child.WaitForExit(900000)) { $child.Kill(); $child.WaitForExit(); throw 'Development build timed out' }
    $child.Refresh()
    if ($null -eq $child.ExitCode -or $child.ExitCode -ne 0 -or -not (Test-Path "$devOutput/angry-chen.exe")) { throw 'Development build failed' }
} finally { $child.Dispose() }
Native node @('tools/p0-ci-artifact-tree.mjs', (Split-Path -Parent $releasePlayer), "$releaseProject/p0-projection.json", 'release', "$OutDir/release-tree.json")
Native node @('tools/p0-ci-artifact-tree.mjs', $devOutput, "$devProject/p0-projection.json", 'development', "$OutDir/development-tree.json")
$releaseTree = Get-Content -Raw "$OutDir/release-tree.json" | ConvertFrom-Json
$developmentTree = Get-Content -Raw "$OutDir/development-tree.json" | ConvertFrom-Json
if ($releaseTree.sourceFingerprint -ne $developmentTree.sourceFingerprint -or $releaseTree.treeSha256 -eq $developmentTree.treeSha256) { throw 'Paired builds require matching frozen inputs and distinct complete player trees (Mono launchers may be identical)' }
foreach ($entry in @(@($releaseProject, 'release'), @($devProject, 'development'))) {
    $report = Join-Path $entry[0] 'Library/LastBuild.buildreport'
    if (-not (Test-Path -LiteralPath $report)) { throw "Missing real BuildReport: $report" }
    Copy-Item -LiteralPath $report -Destination "$OutDir/$($entry[1]).buildreport"
}
# Always execute both halves and write paired verdict even when a scoped gate is red.
$codes = @()
foreach ($role in @('release', 'allocation')) {
    $player = if ($role -eq 'release') { $releasePlayer } else { "$devOutput/angry-chen.exe" }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File client/tools/frame-bench.ps1 -Project $releaseProject -Player $player -Mechanism player -EvidenceRole $role -SourceFingerprint $fingerprint -QualityTier 2 -Runs 3 -WarmupFrames 120 -Frames 600 -Out "$OutDir/$role.json" -OutDir "$OutDir/$role-runs"
    $codes += $LASTEXITCODE
}
Native node @('client/tools/frame-bench-pair.mjs', "$OutDir/release.json", "$OutDir/allocation.json", "$OutDir/paired.json")
if (@($codes | Where-Object { $_ -ne 0 }).Count) { throw 'Scoped benchmark failed' }
Native cmake @('-S', 'server', '-B', "$Scratch/server", '-DAC_WERROR=ON')
Native cmake @('--build', "$Scratch/server", '--config', 'Release', '--parallel', '4')
# Finite P0: frozen two/four-peer impairment+recovery cases only, not a P4 matrix.
Run 'client/tools/player-acceptance.ps1' @('-Player', $releasePlayer, '-ServerExe', "$Scratch/server/Release/ac_server.exe", '-Players', '2', '-DelayMs', '50', '-LossPct', '5', '-Reorder', '-Duplicate', '-Recovery', '-StableSeconds', '15', '-OutDir', "$OutDir/network-2-recovery")
Run 'client/tools/player-acceptance.ps1' @('-Player', $releasePlayer, '-ServerExe', "$Scratch/server/Release/ac_server.exe", '-Players', '4', '-DelayMs', '100', '-LossPct', '10', '-Reorder', '-Duplicate', '-Recovery', '-StableSeconds', '15', '-OutDir', "$OutDir/network-4-recovery")
Write-Output 'P0 ENGINE PASS: selftest + PlayMode + release/development pair + finite 2/4 peer network'
