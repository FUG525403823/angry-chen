# C01 §5.3 / C15 §5：client 的唯一构建入口（发布通道）。
# 用法：powershell -NoProfile -File client/build.ps1 [-Target Windows64] [-Output <目录或 .exe 路径>] [-Backend il2cpp|mono|auto]
# 退出码：0 = 出包成功；1 = 构建失败；2 = 环境缺失（编辑器不可用）。
# 产物：client/Build/Windows64/ac-client-<semver>+<sha7>-win64.zip + 同名 .sha256 + latest.txt
[CmdletBinding()]
param(
    [string]$Target = 'Windows64',
    [string]$Output = 'Build/Windows64',
    [string]$Backend = 'auto'
)

$ErrorActionPreference = 'Stop'

$clientRoot = $PSScriptRoot
$repoRoot = Split-Path -Parent $clientRoot
$editor = $env:AC_UNITY
if (-not $editor) {
    $editor = (Get-ItemProperty -Path 'HKCU:\Environment' -Name 'AC_UNITY' -ErrorAction SilentlyContinue).AC_UNITY
}
if (-not $editor -or -not (Test-Path -LiteralPath $editor)) {
    Write-Output "环境缺失：AC_UNITY 未指向可用的编辑器（当前值：'$editor'）"
    exit 2
}

# §9 冻结：semver / proto 的唯一来源是 Core/VersionInfo.cs，构建脚本只读它，不另立一份。
$versionSource = Join-Path $clientRoot 'Assets/Scripts/Core/VersionInfo.cs'
$versionText = Get-Content -LiteralPath $versionSource -Raw -Encoding UTF8
$semver = ([regex]'SemanticVersion\s*=\s*"([^"]+)"').Match($versionText).Groups[1].Value
$proto = ([regex]'ProtocolVersion\s*=\s*(\d+)').Match($versionText).Groups[1].Value
if (-not $semver -or -not $proto) { Write-Output "构建失败：无法从 VersionInfo.cs 读出 semver/proto"; exit 1 }

$sha7 = (& git -C $repoRoot rev-parse --short=7 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sha7) { Write-Output '构建失败：读不到 git commit'; exit 1 }
$buildTime = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')

# 扫描时把 commit / 构建时间写进生成文件（VersionInfo 不接受运行时手改）
$buildInfoPath = Join-Path $clientRoot 'Assets/Scripts/Core/BuildInfo.g.cs'
Set-Content -LiteralPath $buildInfoPath -Encoding UTF8 -Value @"
// 由 client/build.ps1 在出包时覆盖（扫描时写入 commit 与构建时间）。
// 仓库里保留占位值，保证"没构建过"也能编译；VersionInfo 不接受运行时手改。
namespace Ac.Core
{
    internal static class BuildInfo
    {
        internal const string Commit = "$sha7";
        internal const string BuildTimeUtc = "$buildTime";
    }
}
"@

# §5：IL2CPP 是发布通道；本机缺 win64_il2cpp 变体时退 Mono 并在产物与验收报告里标注 backend。
$il2cppAvailable = @(Get-ChildItem -Path (Join-Path $editor 'Data/PlaybackEngines') -Recurse -Directory -Filter 'win64_il2cpp*' -ErrorAction SilentlyContinue).Count -gt 0
$effectiveBackend = $Backend
if ($Backend -eq 'auto') { $effectiveBackend = if ($il2cppAvailable) { 'il2cpp' } else { 'mono' } }
if ($effectiveBackend -eq 'il2cpp' -and -not $il2cppAvailable) { $effectiveBackend = 'mono' }
Write-Output ("[build] backend=$effectiveBackend (win64_il2cpp present=" + $il2cppAvailable + ")")
Write-Output ("[build] version=ac-client " + $semver + "+" + $sha7 + " proto=" + $proto)

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
    ' -executeMethod Ac.Editor.BuildEntry.BuildWindows64 -logFile - -target ' + $Target + ' -output "' + $outputDir + '"' +
    ' -backend ' + $effectiveBackend
Set-Content -LiteralPath $batchPath -Encoding ASCII -Value ('"' + $editor + '" ' + $engineArgs + ' > "' + $logPath + '" 2> "' + $errorPath + '"')

& cmd.exe /c $batchPath
$code = $LASTEXITCODE
$log = @(Get-Content -LiteralPath $logPath -Encoding UTF8 -ErrorAction SilentlyContinue) +
    @(Get-Content -LiteralPath $errorPath -Encoding UTF8 -ErrorAction SilentlyContinue)

if ($code -ne 0 -or -not (Test-Path -LiteralPath $exePath)) {
    Write-Output "[build] failed exit=$code log=$logPath"
    $log | Select-Object -Last 30 | ForEach-Object { Write-Output ('  ' + $_) }
    exit 1
}

# §5 产物命名与清单：ac-client-<semver>+<sha7>-win64.zip，sha256 清单 + latest.txt（归档名 + 空格 + sha256）
$archiveName = 'ac-client-' + $semver + '+' + $sha7 + '-win64.zip'
$archivePath = Join-Path $outputDir $archiveName
if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
# 发布包只装该发的东西：Unity 自己标了 DoNotShip 的 Burst 调试目录、以及清单文件本身都不进包。
$toShip = @(Get-ChildItem -LiteralPath $outputDir | Where-Object {
    $_.Name -notlike '*_BurstDebugInformation_DoNotShip' -and
    $_.Name -notlike '*.zip' -and $_.Name -notlike '*.sha256' -and
    $_.Name -ne 'latest.txt' -and $_.Name -ne 'manifest.json' })
Compress-Archive -Path ($toShip | ForEach-Object { $_.FullName }) -DestinationPath $archivePath -CompressionLevel Optimal
# 自检：归档必须自包含（没有 exe 或没有 _Data 的包就是废包）。注意 zip 条目用反斜杠。
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipCheck = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
$hasExe = @($zipCheck.Entries | Where-Object { $_.FullName -eq 'angry-chen.exe' }).Count -gt 0
$hasData = @($zipCheck.Entries | Where-Object { $_.FullName -like 'angry-chen_Data*' }).Count -gt 0
$zipCheck.Dispose()
if (-not $hasExe -or -not $hasData) { Write-Output '构建失败：归档缺 angry-chen.exe 或 angry-chen_Data'; exit 1 }
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($archivePath + '.sha256') -Encoding ASCII -Value ($hash + '  ' + $archiveName)
Set-Content -LiteralPath (Join-Path $outputDir 'latest.txt') -Encoding ASCII -Value ($archiveName + ' ' + $hash)
$manifest = @{
    archive = $archiveName
    sha256 = $hash
    semver = $semver
    commit = $sha7
    proto = $proto
    backend = $effectiveBackend
    builtUtc = $buildTime
    versionLine = 'ac-client ' + $semver + '+' + $sha7 + ' proto=' + $proto
}
($manifest | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $outputDir 'manifest.json') -Encoding UTF8

$log | Where-Object { $_ -match '\[build\] backend=' } | Select-Object -Last 1 | ForEach-Object { Write-Output ('  ' + $_) }
Write-Output ('[build] archive=' + $archivePath)
Write-Output ('[build] sha256=' + $hash)
Write-Output ('BUILD OK ' + $archiveName)
exit 0
