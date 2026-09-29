# C15 §5 的六步联调编排入口：起真服务端 + ac_bot 队友，再让客户端批处理自检里的 joint.acceptance
# 真打一遍（UDP 游戏面 + HTTP 诊断面两面都核）。
#
# 用法：powershell -NoProfile -File client/tools/joint-acceptance.ps1
# 退出码：0 = 本脚本的判据全通过；1 = 判据失败（含客户端自检失败）；2 = 环境缺失（服务端/编辑器/端口）。
# 证据：build/joint/ 下的原始回显（server.log、bots.log、selftest.out.txt、joint-lines.txt、
#       server-version.txt、server-metrics-version.txt、health-*.json、matches-recent.json、summary.txt）。
[CmdletBinding()]
param(
    [string]$ServerExe = 'server/build/ac_server.exe',
    [string]$BotExe = 'server/build/ac_bot.exe',
    [int]$UdpPort = 8788,
    [int]$HttpPort = 8787,
    [int]$Bots = 3,
    [int]$BoxMs = 90000,
    [string]$PlayerName = '牧羊人阿',
    [string]$DataDir = 'build/joint-data',
    [string]$OutDir = 'build/joint'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot

$serverPath = Join-Path $repoRoot $ServerExe
$botPath = Join-Path $repoRoot $BotExe
if (-not (Test-Path -LiteralPath $serverPath)) { Write-Output "环境缺失：找不到 $serverPath（先跑 server/build.ps1）"; exit 2 }
if (-not (Test-Path -LiteralPath $botPath)) { Write-Output "环境缺失：找不到 $botPath"; exit 2 }

$outPath = Join-Path $repoRoot $OutDir
New-Item -ItemType Directory -Force -Path $outPath | Out-Null
# 干净的房间：旧战绩会污染第 5 步的"最新一条"，旧宽限名额会把新会话挡在门外（实测过）。
$dataPath = Join-Path $repoRoot $DataDir
if (Test-Path -LiteralPath $dataPath) { Remove-Item -LiteralPath $dataPath -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dataPath | Out-Null

$healthUrl = "http://127.0.0.1:$HttpPort/health"
$metricsUrl = "http://127.0.0.1:$HttpPort/metrics"
$recentUrl = "http://127.0.0.1:$HttpPort/api/matches/recent?limit=2"

function Get-Health() {
    return (Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 3).Content | ConvertFrom-Json
}

# HTTP 诊断面有 30 次/分的滑动窗口（server.hpp 的 kReadRequestsPerMinute），脚本必须自己节流；
# 被限流的请求本身不计数，所以这里退避重试是安全的（联调时 250ms 轮询踩过一次 429）。
function Get-Text([string]$url) {
    for ($k = 0; $k -lt 6; $k++) {
        try { return (Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 3).Content } catch { Start-Sleep -Milliseconds 800 }
    }
    return $null
}

# 版本行：与服务端 /metrics 的标签同源（同一份 core/version.hpp），下面交叉核对。
$serverLine = (& $serverPath --version | Select-Object -First 1).Trim()
Write-Output "[joint-acceptance] 服务端版本行：$serverLine"
$serverLine | Out-File -Encoding utf8 (Join-Path $outPath 'server-version.txt')

$serverProc = $null
$botProc = $null
$code = 1
$summary = New-Object System.Collections.Generic.List[string]
function Note([string]$text) { Write-Output $text; $summary.Add($text) }

try {
    $serverProc = Start-Process -FilePath $serverPath -PassThru -NoNewWindow `
        -ArgumentList @('--serve', "--udp-port=$UdpPort", "--http-port=$HttpPort", "--data-dir=$dataPath", '--minutes=10') `
        -RedirectStandardOutput (Join-Path $outPath 'server.log') -RedirectStandardError (Join-Path $outPath 'server.err.log')

    $up = $false
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        try { $h = Get-Health; $up = $true; break } catch { }
    }
    if (-not $up) { Note '联调失败：服务端 10s 内没有起来（HTTP 诊断面不可达）'; exit 2 }
    Note "[joint-acceptance] 服务端已就绪：$($h | ConvertTo-Json -Compress)"

    # 第 1 步的一半：/metrics 的版本标签必须与 --version 行同源。
    $metrics = (Invoke-WebRequest -Uri $metricsUrl -UseBasicParsing -TimeoutSec 5).Content
    $label = (($metrics -split "`n") | Where-Object { $_ -like 'ac_server_version{*' }) -join ''
    Write-Output "[joint-acceptance] /metrics 版本标签：$label"
    $label | Out-File -Encoding utf8 (Join-Path $outPath 'server-metrics-version.txt')
    if ($label -notlike "*version=`"$($serverLine.Split(' ')[1])`"*") {
        $code = 1
        Note '联调失败：/metrics 的 version 标签与 --version 行不一致（版本行不同源）'
    }

    # 客户端批处理自检（后台起）：环境变量由本进程继承给 Tuanjie.exe；joint.acceptance 在里面真打六步。
    $env:AC_JOINT_UDP = "127.0.0.1:$UdpPort"
    $env:AC_JOINT_SERVER_LINE = $serverLine
    $env:AC_JOINT_NAME = $PlayerName
    $env:AC_JOINT_BOTS = "$Bots"
    $env:AC_JOINT_TEAM = "$($Bots + 1)"
    $env:AC_JOINT_BOX_MS = "$BoxMs"
    # 上一次的日志会污染 [joint] 行的提取（旧 result 行会被当成这一轮的证据）。
    Remove-Item -LiteralPath (Join-Path $repoRoot 'client/Logs/selftest.log') -Force -ErrorAction SilentlyContinue
    # 第 5 步挑战绩记录的时间下界：只认本轮联调期间开始的对局（客户端已经不在房里的下一局不算）。
    $selftestStartMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $selftest = Start-Process -FilePath 'powershell' -PassThru -NoNewWindow `
        -ArgumentList @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'selftest.ps1')) `
        -RedirectStandardOutput (Join-Path $outPath 'selftest.out.txt') `
        -RedirectStandardError (Join-Path $outPath 'selftest.err.txt')

    # 顺序是刚需：客户端必须先在**大厅**里坐定，队友才能进场。ac_bot 一进房就按准备，若它先到，房间会在
    # 本机进房之前就开一局（3 个 bot 全准备 = areAllPlayersReady 成立），本机随后撞上的就是"对局进行中
    # 拒绝准入"——那条路径由客户端用例的第 4 步单独覆盖，不能拿它顶替第 2 步的大厅判据。
    # 节流：每 5s 探一次（12 次/分），300s 上限。
    $clientInRoom = $false
    for ($i = 0; $i -lt 60 -and -not $selftest.HasExited; $i++) {
        Start-Sleep -Seconds 5
        $h = $null
        try { $h = Get-Health } catch { }
        # 两个字段都要成立：connections 是握手在册数，players 是在房连接数。
        if ($h -and $h.connections -ge 1 -and $h.players -ge 1) { $clientInRoom = $true; break }
    }
    if (-not $clientInRoom) { Note '联调失败：300s 内客户端没有进房（joint.acceptance 没走到第 2 步）' }
    else { Note "[joint-acceptance] 客户端已在大厅：$($h | ConvertTo-Json -Compress)" }

    # 队友：把房间占满（第 2 步的名额上限、第 3/4 步的对局都需要人）。
    if ($Bots -gt 0) {
        $botProc = Start-Process -FilePath $botPath -PassThru -NoNewWindow `
            -ArgumentList @("--players $Bots", '--minutes 6', '--host 127.0.0.1', "--port $UdpPort") `
            -RedirectStandardOutput (Join-Path $outPath 'bots.log') -RedirectStandardError (Join-Path $outPath 'bots.err.log')
        $roomFull = $false
        for ($i = 0; $i -lt 15; $i++) {
            Start-Sleep -Seconds 2
            try {
                $h = Get-Health
                if ($h.players -ge ($Bots + 1)) { $roomFull = $true; break }
            } catch { }
        }
        if (-not $roomFull) { Note '联调失败：队友 30s 内没有进房' }
        else { Note "[joint-acceptance] 满房：$($h | ConvertTo-Json -Compress)" }
    }

    $selftest.WaitForExit()
    # `Start-Process -PassThru` 的 ExitCode 不会自己刷新（读到 $null ⇒ 最后一行打成"FAIL（退出码 ）"
    # 并且 `exit $null` 变成 0：摘要说失败、退出码说成功）。必须 Refresh 之后再读。
    $selftest.Refresh()
    $selftestCode = $selftest.ExitCode
    Note "[joint-acceptance] 客户端自检退出码 $selftestCode"
    Get-Content -LiteralPath (Join-Path $outPath 'selftest.out.txt') -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Output $_ }

    # 客户端侧原始回显（[joint] 行直接来自 Unity 日志）。
    $logPath = Join-Path $repoRoot 'client/Logs/selftest.log'
    $jointLines = @()
    $sawSelftestOk = $false
    if (Test-Path -LiteralPath $logPath) {
        $jointLines = @(Select-String -LiteralPath $logPath -Pattern '\[joint\]' | ForEach-Object { $_.Line })
        # 判据以**日志里的 SELFTEST OK 行**为准：Unity 批处理退出码在本机偶发为 0/1 抖动，
        # 只有 `SELFTEST OK cases=N` 才说明用例真的跑完且全绿（原脚本注释里也这么写）。
        $sawSelftestOk = @(Select-String -LiteralPath $logPath -Pattern 'SELFTEST OK cases=').Count -gt 0
    }
    $jointLines | Out-File -Encoding utf8 (Join-Path $outPath 'joint-lines.txt')
    foreach ($line in $jointLines) { Write-Output $line }
    if (-not $sawSelftestOk) { Note '联调失败：日志里没有 SELFTEST OK 行（用例没跑完或没全绿）'; $code = 1 }
    if ($jointLines.Count -eq 0) { Note '联调失败：日志里没有 [joint] 行（joint.acceptance 没跑）'; $code = 1 }
    elseif ($jointLines -match 'skipped') { Note '联调失败：joint.acceptance 自己被跳过（AC_JOINT_UDP 没传进去）'; $code = 1 }

    # 第 5 步：客户端观察 vs 服务端战绩记录。战绩是**对局结束**那一刻才入库的，而客户端的六步验收
    # 可能在时间盒中途就全部满足并收工（本轮实测：客户端把 step3/4/6 都跑完时对局仍在进行，ended=0），
    # 所以这里要等它落库 —— 机器人还在房里打，一局全灭后立刻就有记录（本机实测约 20s）。
    # /health 与 /api/matches/recent 都有限流，按 5s 退避轮询，最多 90s。
    $record = $null
    for ($i = 0; $i -lt 18; $i++) {
        Start-Sleep -Seconds 5
        $healthAfter = Get-Text $healthUrl
        $recent = Get-Text $recentUrl
        if (-not $recent) { continue }
        $entries = ($recent | ConvertFrom-Json).entries
        if (-not $entries -or $entries.Count -eq 0) { continue }
        # 取"本轮联调期间开始的那一局"里最新的一条：避免拿下一局（客户端已经不在房里）的记录去对账。
        $record = $entries | Where-Object { $_.startedAtMs -ge ($selftestStartMs - 120000) } | Select-Object -First 1
        if ($record) { break }
    }
    if (-not $healthAfter -or -not $recent) { Note '联调失败：第 5 步取 /health 或 /api/matches/recent 失败（限流未退避成功）'; $code = 1 }
    $healthAfter | Out-File -Encoding utf8 (Join-Path $outPath 'health-after.json')
    $recent | Out-File -Encoding utf8 (Join-Path $outPath 'matches-recent.json')
    Note "[joint-acceptance] health=$(($healthAfter | ConvertFrom-Json) | ConvertTo-Json -Compress)"
    Note "[joint-acceptance] recent=$recent"

    $resultLine = $jointLines | Where-Object { $_ -like '*result ended=*' } | Select-Object -Last 1
    if ($resultLine -match 'ended=(\d+) phase=(\d+) wave=(\d+)') {
        $ended = [int]$Matches[1]
        $clientWave = [int]$Matches[3]
        if (-not $record) {
            Note '联调失败：第 5 步 90s 内没有战绩记录（对局始终没结束）'
            $code = 1
        } elseif ($ended -eq 1) {
            Note "[joint-acceptance] 第 5 步：客户端 wave=$clientWave 战绩 waveReached=$($record.waveReached) winnerTeam=$($record.winnerTeam) durationMs=$($record.durationMs)"
            if ($clientWave -ne $record.waveReached) { Note '联调失败：第 5 步 waveReached 与客户端观察不一致'; $code = 1 }
        } else {
            Note "[joint-acceptance] 第 5 步：时间盒内对局未结束（ended=0），战绩记录口径 waveReached=$($record.waveReached) winnerTeam=$($record.winnerTeam) durationMs=$($record.durationMs)"
        }
    } else {
        Note '联调失败：第 5 步缺少客户端 result 行'
        $code = 1
    }

    # 第 6 步的另一半（40s 断网 ⇒ 名额释放）：停掉队友，等宽限期把名额吐出来。
    # 采样 3s 一次：30 次/分的读窗口不允许 1s 轮询（限流后的读数会整段缺失）。
    if ($botProc -and -not $botProc.HasExited) { Stop-Process -Id $botProc.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
    $gracePeak = 0
    $sawGrace = $false
    for ($i = 0; $i -lt 12; $i++) {
        Start-Sleep -Seconds 3
        $g = $null
        try { $g = (Get-Health).graceActive } catch { }
        if ($null -eq $g) { continue }
        if ($g -gt $gracePeak) { $gracePeak = $g }
        if ($g -ge 1) { $sawGrace = $true; break }
    }
    $released = $false
    for ($i = 0; $i -lt 16; $i++) {
        Start-Sleep -Seconds 3
        $g = $null
        try { $g = (Get-Health).graceActive } catch { }
        if ($null -ne $g -and $g -eq 0) { $released = $true; break }
    }
    Note "[joint-acceptance] 第 6 步：全部会话断开后宽限期峰值 graceActive=$gracePeak，30s 宽限到期后释放=$(if ($released) { '是' } else { '否' })"
    if (-not $sawGrace -or -not $released) { Note '联调失败：第 6 步的名额释放（ADR-006 的 30s 宽限）没有按预期收尾'; $code = 1 }

    # 到这里六步判据全过才清零（`$code` 初值 1 = "默认失败"，中途任何一条判据都能把它改回去）。
    # 少了这一行脚本永远打 FAIL：之前它靠 `$code = $selftest.ExitCode` 把初值冲成 $null 才没暴露，
    # 而 `exit $null` = 0 —— 摘要说失败、退出码说成功，两边都不算判据。
    $code = 0
}
finally {
    if ($botProc -and -not $botProc.HasExited) { Stop-Process -Id $botProc.Id -Force -ErrorAction SilentlyContinue }
    if ($serverProc -and -not $serverProc.HasExited) { Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue }
    $summary | Out-File -Encoding utf8 (Join-Path $outPath 'summary.txt')
}

if ($code -eq 0) { Write-Output 'JOINT-ACCEPTANCE PASS' } else { Write-Output "JOINT-ACCEPTANCE FAIL（退出码 $code）" }
exit $code
