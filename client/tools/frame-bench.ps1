<#
  C14 frame benchmark gate (plan section 5 / section 6).
  Rules: never pass -nographics (section 5 measurement rule 1). A machine without a graphics
  device cannot produce the section 5 numbers, so it must end in exit code 2 - never PASS.
  Usage: pwsh -File client/tools/frame-bench.ps1 [-Unity <exe>] [-Runs 3]
  Exit: 0 = PASS, 1 = FAIL (over budget), 2 = environment unusable / metrics unmeasurable.
#>
param(
    [string]$Unity = $env:AC_UNITY,
    [string]$Project = "client",
    [string]$Scene = "bench-4p60sheep",
    [string]$Out = "client/TestResults/frame-bench.json",
    [string]$OutDir = "client/Logs/frame-bench",
    [int]$Runs = 3,
    [int]$WarmupFrames = 120,
    [int]$Frames = 600,
    [int]$QualityTier = -1
)
$ErrorActionPreference = "Stop"
$Budget = @{ frameP95Ms = 20.0; frameP99Ms = 33.0; managedAllocBytesPerFrame = 0; gc0Delta = 0; drawCalls = 120; triangles = 180000; particles = 256; materials = 24 }
$StageBudget = @{ input = 0.5; sync = 1.5; predict = 1.0; fx = 4.0; audio = 1.0; draw = 10.0; overlay = 1.0; hud = 1.0 }
$Meta = @("machine","cpu","gpu","driver","unityVersion","resolution","qualityTier","scene","warmupFrames","sampleFrames","runs","commit","verdict")
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
    if ($s.Count % 2 -eq 1) { return $s[[int][Math]::Floor($s.Count / 2)] }
    $hi = [int]($s.Count / 2)
    return ($s[$hi - 1] + $s[$hi]) / 2.0
}
$results = @()
for ($i = 1; $i -le $Runs; $i++) {
    Write-Host ("run " + $i + "/" + $Runs + " warmup " + $WarmupFrames + " sample " + $Frames)
    # Unity's process working directory is the project folder: always pass absolute paths.
    $json = (Get-Location).Path + "/" + $OutDir + "/run-$stamp-$i.json"
    $tierArg = @()
    if ($QualityTier -ge 0) { $tierArg = @("-frameBenchQuality", $QualityTier) }
    & $Unity -batchmode -projectPath $Project -logFile (Join-Path $OutDir "run-$stamp-$i.log") -executeMethod Ac.Tests.FrameBench.Run -frameBenchScene $Scene -frameBenchOut $json -frameBenchWarmup $WarmupFrames -frameBenchSample $Frames -frameBenchRuns $Runs @tierArg
    # 没有数字就谈不上"超预算"：这在 §9 里是"环境不可用"（2），不是 FAIL（1）。
    if (-not (Test-Path $json)) { Write-Host "ENV: run $i produced no JSON (editor could not run the bench - no verdict is possible)"; exit 2 }
    try { $sample = Get-Content -Raw -Encoding UTF8 $json | ConvertFrom-Json } catch { Write-Host "FAIL: run $i JSON parse error"; exit 1 }
    foreach ($f in $Meta) { if ($null -eq $sample.$f) { Write-Host "FAIL: run $i missing field $f"; exit 1 } }
    foreach ($f in @("resolution", "qualityTier", "scene")) { if ([string]::IsNullOrWhiteSpace([string]$sample.$f)) { Write-Host "FAIL: run $i field $f is empty"; exit 1 } }
    if ([string]$sample.scene -ne $Scene) { Write-Host "FAIL: run $i scene is $($sample.scene), expected $Scene"; exit 1 }
    foreach ($k in $Budget.Keys) { if ($null -eq $sample.$k) { Write-Host "FAIL: run $i missing metric $k"; exit 1 } }
    # Fail closed: an unmeasured metric (-1) is NOT "under budget". -1 -gt 120 is false, so without
    # this check a Null-graphics-device run would sail through as PASS.
    foreach ($k in @("drawCalls","triangles","particles","materials")) {
        if ([double]$sample.$k -lt 0) { Write-Host "ENV: $k not measurable on this machine (no graphics device) - cannot judge section 5"; exit 2 }
    }
    if ([string]$sample.gpu -match "Null Device|none" -or [string]$sample.resolution -eq "headless") {
        Write-Host "ENV: no graphics device (gpu=$($sample.gpu) resolution=$($sample.resolution)) - section 5 needs a real display"; exit 2
    }
    foreach ($s in $StageBudget.Keys) { if ($null -eq $sample.stageP95.$s) { Write-Host "FAIL: run $i missing stage $s (stage was never marked - it cannot be judged)"; exit 1 } }
    $results += $sample
}
$failures = @()
foreach ($k in $Budget.Keys) { $v = Get-Median @($results | ForEach-Object { [double]$_.$k }); if ($v -gt $Budget[$k]) { $failures += "$k=$v > $($Budget[$k])" } }
foreach ($s in $StageBudget.Keys) { $v = Get-Median @($results | ForEach-Object { [double]$_.stageP95.$s }); if ($v -gt $StageBudget[$s]) { $failures += "stageP95.$s=$v > $($StageBudget[$s])" } }
$summary = [ordered]@{}
$summary["commit"] = $commit
$summary["runs"] = $Runs
$summary["warmupFrames"] = $WarmupFrames
$summary["sampleFrames"] = $Frames
foreach ($k in $Meta) { if ($k -ne "verdict" -and $k -ne "commit") { $summary[$k] = $results[0].$k } }
foreach ($k in $Budget.Keys) { $summary[$k] = Get-Median @($results | ForEach-Object { [double]$_.$k }) }
$summary["stageP95"] = [ordered]@{}
foreach ($s in $StageBudget.Keys) { $summary["stageP95"][$s] = Get-Median @($results | ForEach-Object { [double]$_.stageP95.$s }) }
$summary["verdict"] = if ($failures.Count -gt 0) { "FAIL" } else { "PASS" }
$summaryPath = (Get-Location).Path + "/" + $Out
$summaryDir = Split-Path $summaryPath -Parent
if (-not (Test-Path $summaryDir)) { New-Item -ItemType Directory -Force -Path $summaryDir | Out-Null }
($summary | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 $summaryPath
($summary | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 (Join-Path $OutDir "summary-$stamp.json")
$p95 = [double]$summary["frameP95Ms"]
$alloc = [double]$summary["managedAllocBytesPerFrame"]
if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Host ("over budget: " + $f) }
    Write-Host ("FRAME-BENCH FAIL p95=" + $p95 + "ms alloc=" + $alloc + "B")
    exit 1
}
Write-Host ("FRAME-BENCH PASS p95=" + $p95 + "ms alloc=" + $alloc + "B")
exit 0
