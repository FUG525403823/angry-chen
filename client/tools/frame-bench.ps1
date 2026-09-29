<#
  C14 frame benchmark gate (plan section 5 / section 6).
  Rules: never pass -nographics (section 5 measurement rule 1). A machine without a graphics
  device cannot produce the section 5 numbers, so it must end in exit code 2 - never PASS.
  The budget table is NOT duplicated here: every number is read back from the per-run JSON
  ($sample.budget.*), which the bench fills from Ac.Core.FrameBudget.
  Usage: pwsh -File client/tools/frame-bench.ps1 [-Unity <exe>] [-Runs 3]
  Exit: 0 = PASS, 1 = FAIL (over budget / schema mismatch), 2 = environment unusable, or the
        run's verdict is not PASS (a synthetic load or an unmeasured metric is never a pass).
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
# 名字在这里列一次即可（判据），预算值一律从 JSON 读。
$MetricNames = @("frameP95Ms", "frameP99Ms", "managedAllocBytesPerFrame", "gc0Delta", "drawCalls", "triangles", "particles", "materials")
$GraphicsMetrics = @("drawCalls", "triangles", "particles", "materials")
$Meta = @("machine", "cpu", "gpu", "driver", "unityVersion", "resolution", "qualityTier", "scene", "sceneKind", "warmupFrames", "sampleFrames", "runs", "commit", "verdict")
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
    # Tuanjie.exe 是 GUI 子系统程序：PowerShell 的调用运算符 & 不等它，脚本会在编辑器写 JSON 之前
    # 就去 Test-Path，于是每次运行都报"没有 JSON"（实测）。必须显式等进程退出后再读它写下的文件。
    $editorArgs = @("-batchmode", "-projectPath", $Project, "-logFile", (Join-Path $OutDir "run-$stamp-$i.log"),
        "-executeMethod", "Ac.Tests.FrameBench.Run", "-frameBenchScene", $Scene, "-frameBenchOut", $json,
        "-frameBenchWarmup", $WarmupFrames, "-frameBenchSample", $Frames, "-frameBenchRuns", $Runs) + $tierArg
    $editor = Start-Process -FilePath $Unity -ArgumentList $editorArgs -NoNewWindow -PassThru
    $editor.WaitForExit()
    Write-Host ("  editor exit code " + $editor.ExitCode)
    # 没有数字就谈不上"超预算"：这在 §9 里是"环境不可用"（2），不是 FAIL（1）。
    if (-not (Test-Path $json)) { Write-Host "ENV: run $i produced no JSON (editor could not run the bench - no verdict is possible)"; exit 2 }
    try { $sample = Get-Content -Raw -Encoding UTF8 $json | ConvertFrom-Json } catch { Write-Host "FAIL: run $i JSON parse error"; exit 1 }
    foreach ($f in $Meta) { if ($null -eq $sample.$f) { Write-Host "FAIL: run $i missing field $f"; exit 1 } }
    foreach ($f in @("resolution", "qualityTier", "scene", "sceneKind", "verdict")) { if ([string]::IsNullOrWhiteSpace([string]$sample.$f)) { Write-Host "FAIL: run $i field $f is empty"; exit 1 } }
    if ([string]$sample.scene -ne $Scene) { Write-Host "FAIL: run $i scene is $($sample.scene), expected $Scene"; exit 1 }
    # 采样窗口必须是本脚本要的那一段，否则分位数来自另一批样本。
    if ([int]$sample.sampleFrames -ne $Frames) { Write-Host "FAIL: run $i sampled $($sample.sampleFrames) frames, expected $Frames"; exit 1 }
    # 预算表的唯一来源是样本自带的 budget 对象（来自 FrameBudget）。没有它 = 基准与门禁模式不匹配，
    # 必须炸出来，绝不回退到脚本里的第二张表。
    if ($null -eq $sample.budget) { Write-Host "FAIL: run $i has no budget object (bench/gate schema mismatch - refusing to guess budgets)"; exit 1 }
    foreach ($k in $MetricNames) {
        if ($null -eq $sample.$k) { Write-Host "FAIL: run $i missing metric $k"; exit 1 }
        if ($null -eq $sample.budget.$k) { Write-Host "FAIL: run $i missing budget.$k"; exit 1 }
    }
    if ($null -eq $sample.budget.stageP95Ms) { Write-Host "FAIL: run $i missing budget.stageP95Ms"; exit 1 }
    # 合成 CPU 负载不是计划里的真实场景：对它判 PASS 就是撒谎。
    if ([string]$sample.sceneKind -ne "plan-scene") { Write-Host "ENV: sceneKind=$($sample.sceneKind) is not 'plan-scene' - a synthetic load is not the section 5 scene"; exit 2 }
    # Fail closed: an unmeasured metric (-1) is NOT "under budget". -1 -gt 120 is false, so without
    # this check a Null-graphics-device run would sail through as PASS.
    foreach ($k in $GraphicsMetrics) {
        if ([double]$sample.$k -lt 0) { Write-Host "ENV: $k not measurable on this machine (no graphics device) - cannot judge section 5"; exit 2 }
    }
    if ([string]$sample.gpu -match "Null Device|none" -or [string]$sample.resolution -eq "headless") {
        Write-Host "ENV: no graphics device (gpu=$($sample.gpu) resolution=$($sample.resolution)) - section 5 needs a real display"; exit 2
    }
    $results += $sample
}
# 预算只从每轮 JSON 读；同时钉住"每轮预算必须一致"，否则中位数没有意义。
$Budget = [ordered]@{}
foreach ($k in $MetricNames) { $Budget[$k] = [double]$results[0].budget.$k }
$StageBudget = [ordered]@{}
foreach ($s in $results[0].budget.stageP95Ms.PSObject.Properties.Name) { $StageBudget[$s] = [double]$results[0].budget.stageP95Ms.$s }
if ($StageBudget.Count -lt 1) { Write-Host "FAIL: budget.stageP95Ms is empty"; exit 1 }
foreach ($r in $results) {
    foreach ($k in $MetricNames) { if ([double]$r.budget.$k -ne [double]$Budget[$k]) { Write-Host "FAIL: budget.$k differs between runs"; exit 1 } }
    foreach ($s in @($StageBudget.Keys)) { if ($null -eq $r.budget.stageP95Ms.$s) { Write-Host "FAIL: budget.stageP95Ms.$s missing in a later run"; exit 1 } }
}
$failures = @()
$measuredStages = @()
$missingStages = @()
foreach ($k in $MetricNames) { $v = Get-Median @($results | ForEach-Object { [double]$_.$k }); if ($v -gt $Budget[$k]) { $failures += "$k=$v > $($Budget[$k])" } }
foreach ($s in @($StageBudget.Keys)) {
    $vals = @($results | ForEach-Object { if ($null -ne $_.stageP95.$s) { [double]$_.stageP95.$s } })
    # 段没被测过（没 Mark）就不能算"预算内"：缺段只记录，判定交给每轮的 verdict。
    if ($vals.Count -ne $results.Count) { $missingStages += $s; continue }
    $measuredStages += $s
    $v = Get-Median $vals
    if ($v -gt $StageBudget[$s]) { $failures += "stageP95.$s=$v > $($StageBudget[$s])" }
}
$notPass = @($results | Where-Object { [string]$_.verdict -ne "PASS" })
$summary = [ordered]@{}
$summary["commit"] = $commit
$summary["runs"] = $Runs
$summary["warmupFrames"] = $WarmupFrames
$summary["sampleFrames"] = $Frames
foreach ($k in $Meta) { if ($k -ne "verdict" -and $k -ne "commit") { $summary[$k] = $results[0].$k } }
foreach ($k in $MetricNames) { $summary[$k] = Get-Median @($results | ForEach-Object { [double]$_.$k }) }
$summary["stageP95"] = [ordered]@{}
foreach ($s in $measuredStages) { $summary["stageP95"][$s] = Get-Median @($results | ForEach-Object { [double]$_.stageP95.$s }) }
$summary["missingStages"] = @($missingStages)
$summary["budget"] = $Budget
$summary["budget"]["stageP95Ms"] = $StageBudget
$summary["verdict"] = if ($failures.Count -gt 0) { "FAIL" } elseif ($notPass.Count -eq 0) { "PASS" } else { "UNVERIFIED" }
$summaryPath = (Get-Location).Path + "/" + $Out
$summaryDir = Split-Path $summaryPath -Parent
if (-not (Test-Path $summaryDir)) { New-Item -ItemType Directory -Force -Path $summaryDir | Out-Null }
$summaryJson = $summary | ConvertTo-Json -Depth 5
Set-Content -Encoding UTF8 $summaryPath -Value $summaryJson
Set-Content -Encoding UTF8 (Join-Path $OutDir "summary-$stamp.json") -Value $summaryJson
# -Out 必须真的落盘：写没写进去是这次判定的前提，不能假设。
if (-not (Test-Path -LiteralPath $summaryPath) -or (Get-Item -LiteralPath $summaryPath).Length -le 0) {
    Write-Host "FAIL: summary was not written to $Out"; exit 1
}
$p95 = [double]$summary["frameP95Ms"]
$alloc = [double]$summary["managedAllocBytesPerFrame"]
# 超预算 = FAIL（1），环境/测不到 = 2：退出码契约见 §9。判定顺序必须"先 FAIL 再 2"，
# 否则一轮超预算会被报成"机器不可用"，把排查引到错误的方向。
$failRuns = @($results | Where-Object { [string]$_.verdict -eq "FAIL" })
if ($failures.Count -gt 0 -or $failRuns.Count -gt 0) {
    foreach ($f in $failures) { Write-Host ("over budget: " + $f) }
    foreach ($r in $failRuns) { Write-Host ("over budget (run verdict FAIL): p95=" + $r.frameP95Ms + " alloc=" + $r.managedAllocBytesPerFrame) }
    Write-Host ("FRAME-BENCH FAIL p95=" + $p95 + "ms alloc=" + $alloc + "B")
    exit 1
}
if ($notPass.Count -gt 0) {
    # verdict 不是 PASS 就不许走 PASS 分支：UNVERIFIED 也是"没有通过"。
    foreach ($r in $notPass) { Write-Host ("verdict=" + $r.verdict + " sceneKind=" + $r.sceneKind + " missingStages=[" + (@($missingStages) -join ",") + "]") }
    exit 2
}
Write-Host ("FRAME-BENCH PASS p95=" + $p95 + "ms alloc=" + $alloc + "B")
exit 0
