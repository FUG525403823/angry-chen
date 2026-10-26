# No Unity or player process is launched: all executables below are inert text fixtures.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/player-discovery.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) ('ac-player-discovery-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $root 'client'
$build = Join-Path $project 'Build/Windows64'
function Assert($condition, [string]$message) { if (-not $condition) { throw $message } }
function Make-Player([string]$dir) {
    New-Item -ItemType Directory -Force -Path (Join-Path $dir 'angry-chen_Data') | Out-Null
    Set-Content -LiteralPath (Join-Path $dir 'angry-chen.exe') -Value 'not executable'
}
try {
    Make-Player $build
    Make-Player (Join-Path $build 'stage/20990101-failed')
    Assert ($null -eq (Resolve-PublishedPlayer $project)) 'must reject legacy executable and unpromoted stage without latest'
    $stage = Join-Path $build 'stage/20261026-good'
    Make-Player $stage
    $release = Join-Path $build 'release/20261026-good'
    New-Item -ItemType Directory -Force -Path $release | Out-Null
    $archive = Join-Path $release 'ac-client-0.1.0+abcdef0-win64.zip'
    Set-Content -LiteralPath $archive -Value 'fixture archive'
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $latest = Join-Path $build 'release/latest.txt'
    $pointer = '20261026-good/ac-client-0.1.0+abcdef0-win64.zip ' + $hash
    Set-Content -LiteralPath $latest -Value $pointer
    $expected = (Get-Item -LiteralPath (Join-Path $stage 'angry-chen.exe')).FullName
    Assert ((Resolve-PublishedPlayer $project) -eq $expected) 'latest must select successful stage despite newer failed stage and legacy exe'
    $explicit = Join-Path $root 'explicit.exe'
    Set-Content -LiteralPath $explicit -Value 'not executable'
    Assert ((Resolve-PublishedPlayer $project $explicit) -eq $explicit) 'explicit -Player must win'
    foreach ($bad in @('', '../escape/archive.zip ' + $hash, '20990101-failed/missing.zip ' + $hash, ($pointer -replace $hash, ('0' * 64)))) {
        Set-Content -LiteralPath $latest -Value $bad
        Assert ($null -eq (Resolve-PublishedPlayer $project)) ('must fail closed for invalid pointer: ' + $bad)
        Assert ((Resolve-PublishedPlayer $project $explicit) -eq $explicit) 'explicit -Player must bypass invalid latest'
    }
    Set-Content -LiteralPath $latest -Value $pointer
    $data = Join-Path $stage 'angry-chen_Data'
    Assert (([IO.Path]::GetFullPath($data)) -eq (Join-Path $build 'stage/20261026-good/angry-chen_Data')) 'unexpected fixture deletion target'
    Remove-Item -LiteralPath $data -Recurse -Force
    Assert ($null -eq (Resolve-PublishedPlayer $project)) 'missing player data must not fall back to failed stage'
    New-Item -ItemType Directory -Force -Path (Join-Path $project 'ProjectSettings') | Out-Null
    Set-Content -LiteralPath (Join-Path $project 'ProjectSettings/ProjectVersion.txt') -Value 'fixture'
    $entry = Join-Path $PSScriptRoot 'frame-bench.ps1'
    $lines = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $entry -Project $project -Mechanism player -Unity 'must-not-launch.exe' 2>&1
    Assert ($LASTEXITCODE -eq 2) 'frame bench must stop before launching when published stage is incomplete'
    Assert (($lines -join "`n") -match 'no published player via') 'frame bench must report the published-player environment failure'
    New-Item -ItemType Directory -Force -Path $data | Out-Null
    Set-Content -LiteralPath $archive -Value 'corrupted archive'
    Assert ($null -eq (Resolve-PublishedPlayer $project)) 'archive hash mismatch must fail closed'

    # Check the production entry uses the tested resolver rather than retaining its legacy scanner.
    $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'frame-bench.ps1') -Raw
    Assert ($source.Contains('$Player = Resolve-PublishedPlayer -Project $Project -Player $Player')) 'frame bench must use resolver with explicit override'
    Assert (-not $source.Contains('function Find-PlayerExe')) 'legacy scanner must be removed'
    Write-Output 'PASS player discovery: published stage, failed-stage exclusion, explicit override, malformed/missing/corrupt release'
} finally {
    if ([IO.Path]::GetFullPath($root) -ne (Join-Path ([IO.Path]::GetTempPath()) (Split-Path $root -Leaf)) -or
        (Split-Path $root -Leaf) -notlike 'ac-player-discovery-*') { throw 'unexpected fixture cleanup target' }
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
