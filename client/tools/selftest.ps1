# C01 §5.3 / C02 §8：仓库内可复现的自检入口（唯一可行通道：cmd + 文件重定向，用管道接子进程 stdio 会挂死）。
# 用法：powershell -NoProfile -File client/tools/selftest.ps1
# 退出码：0 = 全绿；否则就是编辑器的退出码（1 = 有用例失败）；2 = 环境缺失（编辑器不可用）。
[CmdletBinding()]
param(
    [string]$Unity = $env:AC_UNITY
)

$ErrorActionPreference = 'Stop'

$clientRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $clientRoot
$logDir = Join-Path $clientRoot 'Logs'
$logPath = Join-Path $logDir 'selftest.log'
$cmdPath = Join-Path $logDir 'selftest.cmd'

if (-not $Unity) {
    $Unity = (Get-ItemProperty -Path 'HKCU:\Environment' -Name 'AC_UNITY' -ErrorAction SilentlyContinue).AC_UNITY
}
if (-not $Unity -or -not (Test-Path -LiteralPath $Unity)) {
    Write-Output "环境缺失：AC_UNITY 未指向可用的编辑器（当前值：'$Unity'）"
    exit 2
}
if (-not (Test-Path -LiteralPath (Join-Path $clientRoot 'ProjectSettings/ProjectVersion.txt'))) {
    Write-Output "环境缺失：$clientRoot 不是 Unity 工程"
    exit 2
}

New-Item -ItemType Directory -Force -Path $logDir | Out-Null
# Tuanjie.exe 是 GUI 子系统程序：交给 cmd 代跑（cmd 会等 GUI 程序结束并把退出码原样传回），
# 输出走文件重定向。路径相对仓库根，所以下面把 cmd 的工作目录切到仓库根。
$engineArgs = '-batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll' +
    ' -logFile client/Logs/selftest.log'
Set-Content -LiteralPath $cmdPath -Encoding ASCII -Value ('"' + $Unity + '" ' + $engineArgs + ' > client/Logs/selftest.stdout.log 2>&1')

Push-Location $repoRoot
try {
    & cmd.exe /c $cmdPath
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if (Test-Path -LiteralPath $logPath) {
    Select-String -LiteralPath $logPath -Pattern '^SELFTEST |^FAIL ' | ForEach-Object { Write-Output $_.Line }
}

if ($null -eq $code) { Write-Output '自检失败：编辑器没有返回退出码'; exit 1 }
if (@(Select-String -LiteralPath $logPath -Pattern '^SELFTEST OK cases=' -ErrorAction SilentlyContinue).Count -eq 0) {
    Write-Output '自检失败：日志里没有 SELFTEST OK 行（用例没跑完，退出码不作数）'
    exit 1
}
exit $code
