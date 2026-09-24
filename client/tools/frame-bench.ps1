<#
  C14 frame benchmark gate (plan section 5).
  Rules: never pass -nographics; fail closed when the numbers are missing.
  Usage: pwsh -File client/tools/frame-bench.ps1 [-Unity <exe>] [-Runs 3]
#>
param(
    [string]$Unity = $env:AC_UNITY,
    [string]$Project = "client",
    [string]$Scene = "bench-4p60sheep",
    [string]$Out = "client/TestResults/frame-bench.json",
    [string]$OutDir = "client/Logs/frame-bench",
    [int]$Runs = 3,
    [int]$WarmupFrames = 120,
    [int]$Frames = 600
)
$ErrorActionPreference = "Stop"
$Budget = @{ frameP95Ms = 20.0; frameP99Ms = 33.0; managedAllocBytesPerFrame = 0; gc0Delta = 0; drawCalls = 120; triangles = 180000; particles = 256; materials = 24 }
$StageBudget = @{ input = 0.5; sync = 1.5; predict = 1.0; fx = 4.0; audio = 1.0; draw = 10.0; overlay = 1.0; hud = 1.0 }
$Meta = @("machine","cpu","gpu","driver","unityVersion","resolution","qualityTier","scene","warmupFrames","sampleFrames","runs","commit","verdict")
# C14 section 9: exit 0 = pass, 1 = over budget, 2 = environment unusable.
if ([string]::IsNullOrWhiteSpace($Unity)) { Write-Host "ENV: no Unity executable (set AC_UNITY or pass -Unity)"; exit 2 }
if ([string]::IsNullOrWhiteSpace($Scene)) { Write-Host "ENV: scene name required (-Scene)"; exit 2 }
if ([string]::IsNullOrWhiteSpace($Out)) { Write-Host "ENV: output path required (-Out)"; exit 2 }
if ($Runs -lt 1) { Write-Host "ENV: -Runs must be >= 1"; exit 2 }
if ($Frames -lt 1) { Write-Host "ENV: -Frames must be >= 1"; exit 2 }
if ([string]::IsNullOrWhiteSpace($Project) -or -not (Test-Path (Join-Path $Project "ProjectSettings/ProjectVersion.txt"))) { Write-Host "ENV: no Unity project at $Project"; exit 2 }
$commit = (git rev-parse --short HEAD).Trim()
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function Get-Median([double[]]$v) {
    $s = @($v | Sort-Object)
    if ($s.Count -eq 0) { return 0.0 }
    # 偶数次取中间两个的均值，而不是上中位（C14 标准轴必改项 8）。
    if ($s.Count % 2 -eq 1) { return $s[[int][Math]::Floor($s.Count / 2)] }
    $hi = [int]($s.Count / 2)
    return ($s[$hi - 1] + $s[$hi]) / 2.0
}
$results = @()
for ($i = 1; $i -le $Runs; $i++) {
    $json = Join-Path $OutDir "run-$stamp-$i.json"
    Write-Host ("run " + $i + "/" + $Runs + " warmup " + $WarmupFrames + " sample " + $SampleFrames)
    & $Unity -batchmode -projectPath $Project -logFile (Join-Path $OutDir "run-$stamp-$i.log") -executeMethod Ac.Tests.FrameBench.Run -frameBenchScene $Scene -frameBenchOut $json -frameBenchWarmup $WarmupFrames -frameBenchSample $Frames
    if (-not (Test-Path $json)) { Write-Host "FAIL: run $i produced no JSON (bench entry point missing - not a pass)"; exit 1 }
    try { $sample = Get-Content -Raw $json | ConvertFrom-Json } catch { Write-Host "FAIL: run $i JSON parse error"; exit 1 }
    foreach ($f in $Meta) { if ($null -eq $sample.$f) { Write-Host "FAIL: run $i missing field $f"; exit 1 } }
    # 空字符串不是"有值"：PS 里 $null -eq "" 为 False，不显式查就会被廉价场景蒙过去。
    foreach ($f in @("resolution", "qualityTier", "scene")) { if ([string]::IsNullOrWhiteSpace([string]$sample.$f)) { Write-Host "FAIL: run $i field $f is empty"; exit 1 } }
    if ([string]$sample.scene -ne $Scene) { Write-Host "FAIL: run $i scene is $($sample.scene), expected $Scene"; exit 1 }
    foreach ($k in $Budget.Keys) { if ($null -eq $sample.$k) { Write-Host "FAIL: run $i missing metric $k"; exit 1 } }
    foreach ($s in $StageBudget.Keys) { if ($null -eq $sample.stageP95.$s) { Write-Host "FAIL: run $i missing stage $s"; exit 1 } }
    $results += $sample
}
$failures = @()
foreach ($k in $Budget.Keys) { $v = Get-Median @($results | ForEach-Object { [double]$_.$k }); if ($v -gt $Budget[$k]) { $failures += "$k=$v > $($Budget[$k])" } }
foreach ($s in $StageBudget.Keys) { $v = Get-Median @($results | ForEach-Object { [double]$_.stageP95.$s }); if ($v -gt $StageBudget[$s]) { $failures += "stageP95.$s=$v > $($StageBudget[$s])" } }
$summary = [ordered]@{ commit = $commit; runs = $Runs; warmupFrames = $WarmupFrames; sampleFrames = $SampleFrames }
foreach ($k in $Meta) { if ($k -ne "verdict" -and $k -ne "commit") { $summary[$k] = $results[0].$k } }
foreach ($k in $Budget.Keys) { $summary[$k] = Get-Median @($results | ForEach-Object { [double]$_.$k }) }
foreach ($s in $StageBudget.Keys) { $summary["stageP95.$s"] = Get-Median @($results | ForEach-Object { [double]$_.stageP95.$s }) }
if ($failures.Count -gt 0) { $summary["verdict"] = "FAIL" } else { $summary["verdict"] = "PASS" }
$summaryPath = Join-Path $OutDir "summary-$stamp.json"
$summary | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $summaryPath
Write-Host "summary (median of $Runs runs) -> $summaryPath"
if ($failures.Count -gt 0) { Write-Host "FRAME-BENCH FAIL"; $failures | ForEach-Object { Write-Host "  - $_" }; exit 1 }
Write-Host "FRAME-BENCH PASS"
exit 0