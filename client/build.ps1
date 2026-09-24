# C01 §5.3 / §9：client 的唯一构建入口。
# 用法：powershell -NoProfile -File client/build.ps1 [-Target Windows64] [-Output <目录或 .exe 路径>]
# 退出码：0 = 出包成功；1 = 构建失败；2 = 环境缺失（编辑器不可用）。
[CmdletBinding()]
param(
    [string]$Target = 'Windows64',
    [string]$Output = 'Build/Windows64'
)

$ErrorActionPreference = 'Stop'

$clientRoot = $PSScriptRoot
$editor = $env:AC_UNITY
if (-not $editor) {
    $editor = (Get-ItemProperty -Path 'HKCU:\Environment' -Name 'AC_UNITY' -ErrorAction SilentlyContinue).AC_UNITY
}
if (-not $editor -or -not (Test-Path -LiteralPath $editor)) {
    Write-Output "环境缺失：AC_UNITY 未指向可用的编辑器（当前值：'$editor'）"
    exit 2
}

if ($Output -like '*.exe') { $exePath = $Output } else { $exePath = Join-Path $Output 'angry-chen.exe' }
if (-not [System.IO.Path]::IsPathRooted($exePath)) { $exePath = Join-Path $clientRoot $exePath }
$outputDir = Split-Path -Parent $exePath

$logDir = Join-Path $clientRoot 'Logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logPath = Join-Path $logDir 'build.log'
$errorPath = Join-Path $logDir 'build.stderr.log'
$batchPath = Join-Path $logDir 'build.cmd'

Write-Output "[build] target=$Target project=$clientRoot"
Write-Output "[build] exe=$exePath"

# Tuanjie.exe 是 GUI 子系统程序：PowerShell 的 & 不等待它结束（$LASTEXITCODE 为空），
# Start-Process -PassThru 的 ExitCode 也取不到值。交给 cmd 代跑：cmd 会等 GUI 程序结束并把退出码原样传回。
$engineArgs = '-batchmode -quit -nographics -projectPath "' + $clientRoot + '"' +
    ' -executeMethod Ac.Editor.BuildEntry.BuildWindows64 -logFile - -target ' + $Target + ' -output "' + $outputDir + '"'
Set-Content -LiteralPath $batchPath -Encoding ASCII -Value ('"' + $editor + '" ' + $engineArgs + ' > "' + $logPath + '" 2> "' + $errorPath + '"')

& cmd.exe /c $batchPath
$code = $LASTEXITCODE
$log = @(Get-Content -LiteralPath $logPath -Encoding UTF8 -ErrorAction SilentlyContinue) +
    @(Get-Content -LiteralPath $errorPath -Encoding UTF8 -ErrorAction SilentlyContinue)

if ($code -eq 0 -and (Test-Path -LiteralPath $exePath)) {
    Write-Output ('[build] log=' + $logPath + ' bytes=' + (Get-Item -LiteralPath $exePath).Length)
    Write-Output 'Build succeeded'
    exit 0
}

Write-Output "[build] failed exit=$code log=$logPath"
$log | Select-Object -Last 30 | ForEach-Object { Write-Output ('  ' + $_) }
exit 1
