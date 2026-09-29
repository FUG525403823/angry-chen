<#
  C14 frame benchmark gate (plan section 5 / section 6).
  Rules: never pass -nographics (section 5 measurement rule 1). A machine without a graphics
  device cannot produce the section 5 numbers, so it must end in exit code 2 - never PASS.
  The budget table is NOT duplicated here: every number is read back from the per-run JSON
  ($sample.budget.*), which the bench fills from Ac.Core.FrameBudget.

  Mechanism (per the ruling that the engine owns the frame loop):
    player  窗口化出包 player 跑正式判定：引擎自己的帧循环渲染（无手动 Camera.Render）。
    editor  -batchmode 编辑器路径，保留为**诊断口径**：它那份数字照旧打印，但不参与判定
            （batchmode 里引擎不做任何渲染，帧时间是手动 Camera.Render() 的地板）。
    auto    默认：有 <Project>/Build/Windows64/*.exe 就用 player，否则退回 editor。
  Usage: pwsh -File client/tools/frame-bench.ps1 [-Runs 3] [-Mechanism auto|editor|player]
  Exit: 0 = PASS, 1 = FAIL (over budget / schema mismatch), 2 = environment unusable, or the
        run's verdict is not PASS (a synthetic load or an unmeasured metric is never a pass).
#>
param(
    [string]$Unity = $env:AC_UNITY,
    [string]$Project = "client",
    [string]$Scene = "bench-4p60sheep",
    [string]$Out = "client/TestResults/frame-bench.json",
    [string]$OutDir = "client/Logs/frame-bench",
    [ValidateSet("auto", "editor", "player")][string]$Mechanism = "auto",
    [string]$Player = "",
    [int]$Runs = 3,
    [int]$WarmupFrames = 120,
    [int]$Frames = 600,
    [int]$QualityTier = -1,
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = "Stop"
# 名字在这里列一次即可（判据），预算值一律从 JSON 读。
$MetricNames = @("frameP95Ms", "frameP99Ms", "managedAllocBytesPerFrame", "gc0Delta", "drawCalls", "triangles", "particles", "materials")
$GraphicsMetrics = @("drawCalls", "triangles", "particles", "materials")
$Meta = @("machine", "cpu", "gpu", "driver", "unityVersion", "resolution", "qualityTier", "scene", "sceneKind", "mechanism", "warmupFrames", "sampleFrames", "runs", "commit", "verdict")

if ([string]::IsNullOrWhiteSpace($Scene)) { Write-Host "ENV: scene name required (-Scene)"; exit 2 }
if ([string]::IsNullOrWhiteSpace($Out)) { Write-Host "ENV: output path required (-Out)"; exit 2 }
if ($Runs -lt 1) { Write-Host "ENV: -Runs must be >= 1"; exit 2 }
if ($Frames -lt 1) { Write-Host "ENV: -Frames must be >= 1"; exit 2 }
if ($TimeoutSeconds -lt 1) { Write-Host "ENV: -TimeoutSeconds must be >= 1"; exit 2 }
if ([string]::IsNullOrWhiteSpace($Project) -or -not (Test-Path (Join-Path $Project "ProjectSettings/ProjectVersion.txt"))) { Write-Host "ENV: no Unity project at $Project"; exit 2 }

function Find-PlayerExe([string]$project) {
    $dir = Join-Path $project "Build/Windows64"
    if (-not (Test-Path $dir)) { return $null }
    # Unity 的 player 布局：<name>.exe 旁边有 <name>_Data 目录。不能只按名字取第一个 ——
    # 同目录里的 TuanjieCrashHandler64.exe 排序在前，取错了就等于拿崩溃处理器当被测进程。
    $exe = @(Get-ChildItem -Path $dir -Filter *.exe -File -ErrorAction SilentlyContinue)
    foreach ($candidate in $exe) {
        if (Test-Path -LiteralPath (Join-Path $dir ($candidate.BaseName + "_Data"))) { return $candidate.FullName }
    }
    return $null
}
if ([string]::IsNullOrWhiteSpace($Player)) { $Player = Find-PlayerExe $Project }
$playerOk = -not [string]::IsNullOrWhiteSpace($Player) -and (Test-Path -LiteralPath $Player)
$unityOk = -not [string]::IsNullOrWhiteSpace($Unity)
if ($Mechanism -eq "auto") {
    $Mechanism = if ($playerOk) { "player" } else { "editor" }
    Write-Host ("mechanism auto -> " + $Mechanism)
}
if ($Mechanism -eq "player" -and -not $playerOk) { Write-Host "ENV: -Mechanism player but no built player at <Project>/Build/Windows64/*.exe (build it with client/build.ps1, or pass -Player)"; exit 2 }
if ($Mechanism -eq "editor" -and -not $unityOk) { Write-Host "ENV: no Unity executable (set AC_UNITY or pass -Unity)"; exit 2 }
if ($Mechanism -eq "player" -and -not $unityOk) { Write-Host "WARN: no Unity executable - the editor diagnostic run is skipped" }
if ($Mechanism -eq "player") { Write-Host ("player exe: " + $Player) }

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

# 跑一次基准并读回它的 JSON。$UsePlayer 决定用哪条机制（engine frame loop / batchmode 手动渲染）。
# 判定一律从 JSON 读：进程自己的退出码只当交叉校验，不当结论。
function Invoke-BenchRun([int]$Index, [bool]$UsePlayer, [string[]]$ExtraArgs = @(), [string]$FileTag = "") {
    $tag = if ($UsePlayer) { "player" } else { "editor" }
    # $FileTag 只影响文件名：同一机制要跑第二个用途（如空场对照）时不能覆盖判定轮的 JSON。
    if ([string]::IsNullOrEmpty($FileTag)) { $FileTag = $tag }
    Write-Host ("run " + $FileTag + " " + $Index + " warmup " + $WarmupFrames + " sample " + $Frames)
    # 两条路径都把输出写成绝对路径：Unity 的进程工作目录是项目目录 / exe 目录，不是这里。
    $json = (Get-Location).Path + "/" + $OutDir + "/run-$stamp-$FileTag-$Index.json"
    $log = (Get-Location).Path + "/" + $OutDir + "/run-$stamp-$FileTag-$Index.log"
    $tierArg = @()
    if ($QualityTier -ge 0) { $tierArg = @("-frameBenchQuality", $QualityTier) }
    $common = @("-logFile", $log, "-frameBenchScene", $Scene, "-frameBenchOut", $json,
        "-frameBenchWarmup", $WarmupFrames, "-frameBenchSample", $Frames, "-frameBenchRuns", $Runs) + $tierArg + $ExtraArgs
    if ($UsePlayer) {
        # player 必须是窗口化的（不许 -batchmode：那样引擎自己不会渲染，量到的就不是这条机制了）。
        # 离线：不能给 player 传 -server —— Unity 播放器自己的参数表也认 -server（<slave_count> <ip:port>），
        # 实测它把进程判成参数错误后直接中止（exit 1，脚本都还没跑到）。改用环境变量 AC_SERVER：
        # GameBootstrap 的解析顺序是 -server → AC_SERVER，而 AC_SERVER=127.0.0.1:0 端口非法 → 不连，
        # 与 editor 路径（isBatchMode/isEditor 一律不连）同语义。
        $previousServer = $env:AC_SERVER
        $env:AC_SERVER = "127.0.0.1:0"
        try {
            $argv = @("-screen-fullscreen", "0") + $common
            $proc = Start-Process -FilePath $Player -ArgumentList $argv -PassThru
        }
        finally {
            if ($null -eq $previousServer) { Remove-Item Env:\AC_SERVER -ErrorAction SilentlyContinue }
            else { $env:AC_SERVER = $previousServer }
        }
    }
    else {
        # Tuanjie.exe 是 GUI 子系统程序：PowerShell 的调用运算符 & 不等它，脚本会在编辑器写 JSON 之前
        # 就去 Test-Path，于是每次运行都报"没有 JSON"（实测）。必须显式等进程退出后再读它写下的文件。
        $argv = @("-batchmode", "-projectPath", $Project) + $common + @("-executeMethod", "Ac.Tests.FrameBench.Run")
        $proc = Start-Process -FilePath $Unity -ArgumentList $argv -NoNewWindow -PassThru
    }
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill() } catch { }
        Write-Host ("ENV: " + $tag + " run " + $Index + " did not exit within " + $TimeoutSeconds + "s (killed) - no verdict is possible")
        exit 2
    }
    Write-Host ("  " + $tag + " exit code " + $proc.ExitCode)
    # 没有数字就谈不上"超预算"：这在 §9 里是"环境不可用"（2），不是 FAIL（1）。
    if (-not (Test-Path -LiteralPath $json)) { Write-Host ("ENV: " + $tag + " run " + $Index + " produced no JSON (the bench could not run - no verdict is possible)"); exit 2 }
    try { $sample = Get-Content -Raw -Encoding UTF8 $json | ConvertFrom-Json } catch { Write-Host ("FAIL: " + $tag + " run " + $Index + " JSON parse error"); exit 1 }
    foreach ($f in $Meta) { if ($null -eq $sample.$f) { Write-Host ("FAIL: " + $tag + " run " + $Index + " missing field " + $f); exit 1 } }
    foreach ($f in @("resolution", "qualityTier", "scene", "sceneKind", "mechanism", "verdict")) { if ([string]::IsNullOrWhiteSpace([string]$sample.$f)) { Write-Host ("FAIL: " + $tag + " run " + $Index + " field " + $f + " is empty"); exit 1 } }
    if ([string]$sample.scene -ne $Scene) { Write-Host ("FAIL: " + $tag + " run " + $Index + " scene is " + $sample.scene + ", expected " + $Scene); exit 1 }
    if ([string]$sample.mechanism -ne $tag) { Write-Host ("FAIL: " + $tag + " run " + $Index + " reports mechanism=" + $sample.mechanism); exit 1 }
    # 采样窗口必须是本脚本要的那一段，否则分位数来自另一批样本。
    if ([int]$sample.sampleFrames -ne $Frames) { Write-Host ("FAIL: " + $tag + " run " + $Index + " sampled " + $sample.sampleFrames + " frames, expected " + $Frames); exit 1 }
    # 预算表的唯一来源是样本自带的 budget 对象（来自 FrameBudget）。没有它 = 基准与门禁模式不匹配，
    # 必须炸出来，绝不回退到脚本里的第二张表。
    if ($null -eq $sample.budget) { Write-Host ("FAIL: " + $tag + " run " + $Index + " has no budget object (bench/gate schema mismatch - refusing to guess budgets)"); exit 1 }
    foreach ($k in $MetricNames) {
        if ($null -eq $sample.$k) { Write-Host ("FAIL: " + $tag + " run " + $Index + " missing metric " + $k); exit 1 }
        if ($null -eq $sample.budget.$k) { Write-Host ("FAIL: " + $tag + " run " + $Index + " missing budget." + $k); exit 1 }
    }
    if ($null -eq $sample.budget.stageP95Ms) { Write-Host ("FAIL: " + $tag + " run " + $Index + " missing budget.stageP95Ms"); exit 1 }
    # 合成 CPU 负载不是计划里的真实场景：对它判 PASS 就是撒谎。
    if ([string]$sample.sceneKind -ne "plan-scene") { Write-Host ("ENV: " + $tag + " run " + $Index + " sceneKind=" + $sample.sceneKind + " is not 'plan-scene' - a synthetic load is not the section 5 scene"); exit 2 }
    # Fail closed: an unmeasured metric (-1) is NOT "under budget". -1 -gt 120 is false, so without
    # this check a Null-graphics-device run would sail through as PASS.
    foreach ($k in $GraphicsMetrics) {
        if ([double]$sample.$k -lt 0) { Write-Host ("ENV: " + $tag + " run " + $Index + " metric " + $k + " not measurable on this machine (no graphics device) - cannot judge section 5"); exit 2 }
    }
    if ([string]$sample.gpu -match "Null Device|none" -or [string]$sample.resolution -eq "headless") {
        Write-Host ("ENV: " + $tag + " run " + $Index + " has no graphics device (gpu=" + $sample.gpu + " resolution=" + $sample.resolution + ") - section 5 needs a real display"); exit 2
    }
    if ($UsePlayer) {
        # "引擎自己拥有帧循环"必须留下可证伪的痕迹：-batchmode 下 Time.frameCount 根本不前进，
        # 而 player 的采样窗口里它必须真的在走（这是 player 机制区别于 editor 机制的唯一实质判据）。
        if ($null -eq $sample.phaseMs -or [double]$sample.phaseMs.engineFrames -le 0) {
            Write-Host ("ENV: player run " + $Index + " reports engineFrames<=0 - the engine never advanced a frame during the sample window"); exit 2
        }
        if ([double]$sample.phaseMs.engineDeltaP95 -le 0) {
            Write-Host ("ENV: player run " + $Index + " reports engineDeltaP95<=0 - no engine frame time was observed"); exit 2
        }
    }
    return $sample
}

$results = @()
$editorDiag = $null
for ($i = 1; $i -le $Runs; $i++) { $results += (Invoke-BenchRun -Index $i -UsePlayer ($Mechanism -eq "player")) }
if ($Mechanism -eq "player") {
    # ADR-014 裁决 4：判 PASS 的第三条不变式 = **同轮**空场景地板低于预算。判据是这一轮里量出来的，
    # 不是别处抄来的：地板 >= 预算 说明这条机制下任何内容都不可能达标（机制/环境不可用 ⇒ 2，不是 FAIL）。
    $emptyControl = Invoke-BenchRun -Index 1 -UsePlayer $true -ExtraArgs @("-frameBenchEmpty", "1") -FileTag "player-empty"
    $emptyP95 = [double]$emptyControl.frameP95Ms
    $emptyBudget = [double]$emptyControl.budget.frameP95Ms
    Write-Host ("  player empty-scene floor: frameP95=" + $emptyP95 + "ms frameP99=" + [double]$emptyControl.frameP99Ms + "ms engineFrames=" + [int]$emptyControl.phaseMs.engineFrames + " pixelCoverage=" + [double]$emptyControl.graphics.pixelCoverage)
    if ($emptyP95 -ge $emptyBudget) {
        Write-Host ("ENV: empty-scene floor under this mechanism is " + $emptyP95 + "ms >= the " + $emptyBudget + "ms frame budget - nothing can pass under this mechanism (no verdict is possible)")
        exit 2
    }
}
if ($Mechanism -eq "player" -and $unityOk) {
    # 诊断口径：照旧打印 editor 那份数字（不参与判定）。一轮即可——它的意义是"这条机制地板有多高"。
    $editorDiag = Invoke-BenchRun -Index 1 -UsePlayer $false
    Write-Host ("  editor diagnostics: frameP95=" + [double]$editorDiag.frameP95Ms + "ms frameP99=" + [double]$editorDiag.frameP99Ms + "ms verdict=" + $editorDiag.verdict)
}
elseif ($Mechanism -eq "editor") {
    $editorDiag = $results[0]
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
$summary["mechanism"] = $Mechanism
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
# 两条机制的数字都留在产物里：判定只跟 $Mechanism 走，另一半是诊断。
$summary["mechanismP95"] = [ordered]@{
    player = if ($Mechanism -eq "player") { [double]$summary["frameP95Ms"] } else { $null }
    editor = if ($null -ne $editorDiag) { [double]$editorDiag.frameP95Ms } else { $null }
    editorVerdict = if ($null -ne $editorDiag) { [string]$editorDiag.verdict } else { $null }
    editorialNote = "editor = -batchmode 诊断口径（引擎不渲染，帧时间是手动 Camera.Render() 的地板）；判定只认当前 -Mechanism"
}
# ADR-014 裁决 4 的第二、三条不变式：同轮的真光栅证据 + 同轮空场景地板，都留在产物里。
$summary["emptyControl"] = if ($null -ne $emptyControl) { [ordered]@{
    mechanism = [string]$emptyControl.mechanism
    frameP95Ms = [double]$emptyControl.frameP95Ms
    frameP99Ms = [double]$emptyControl.frameP99Ms
    budgetFrameP95Ms = [double]$emptyControl.budget.frameP95Ms
    engineFrames = [int]$emptyControl.phaseMs.engineFrames
    pixelCoverage = [double]$emptyControl.graphics.pixelCoverage
    belowBudget = [bool]([double]$emptyControl.frameP95Ms -lt [double]$emptyControl.budget.frameP95Ms)
    note = "同轮空场（-frameBenchEmpty 1，只关来件与帧回路、停用场地网格）的地板：判 PASS 的机制不变式之一"
} } else { $null }
$summary["rasterEvidence"] = [ordered]@{
    planScenePixelCoverage = [double]$results[0].graphics.pixelCoverage
    emptyScenePixelCoverage = if ($null -ne $emptyControl) { [double]$emptyControl.graphics.pixelCoverage } else { $null }
}
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
# 末行必须保住 §6/C15 冻结的判据前缀 `FRAME-BENCH <VERDICT> p95=<x>ms alloc=<n>B`：两条机制的数字接在它后面，
# 判定只认 -Mechanism 那条。位置/名字换了会让人以为门禁的输出契约变了。
$tail = " alloc=" + $alloc + "B mechanism=" + $Mechanism
if ($null -ne $emptyControl) { $tail += " emptyP95=" + [double]$emptyControl.frameP95Ms + "ms" }
if ($Mechanism -eq "player" -and $null -ne $editorDiag) { $tail += " editorP95=" + [double]$editorDiag.frameP95Ms + "ms editorVerdict=" + $editorDiag.verdict }
if ($Mechanism -eq "editor") { $tail += " (player round not run: -Mechanism editor)" }
$head = "p95=" + $p95 + "ms"
# 超预算 = FAIL（1），环境/测不到 = 2：退出码契约见 §9。判定顺序必须"先 FAIL 再 2"，
# 否则一轮超预算会被报成"机器不可用"，把排查引到错误的方向。
$failRuns = @($results | Where-Object { [string]$_.verdict -eq "FAIL" })
if ($failures.Count -gt 0 -or $failRuns.Count -gt 0) {
    foreach ($f in $failures) { Write-Host ("over budget: " + $f) }
    foreach ($r in $failRuns) { Write-Host ("over budget (run verdict FAIL): p95=" + $r.frameP95Ms + " alloc=" + $r.managedAllocBytesPerFrame) }
    Write-Host ("FRAME-BENCH FAIL " + $head + $tail)
    exit 1
}
if ($notPass.Count -gt 0) {
    # verdict 不是 PASS 就不许走 PASS 分支：UNVERIFIED 也是"没有通过"。
    foreach ($r in $notPass) { Write-Host ("verdict=" + $r.verdict + " mechanism=" + $r.mechanism + " sceneKind=" + $r.sceneKind + " missingStages=[" + (@($missingStages) -join ",") + "]") }
    Write-Host ("FRAME-BENCH NOT-PASS " + $head + $tail)
    exit 2
}
Write-Host ("FRAME-BENCH PASS " + $head + $tail)
exit 0
