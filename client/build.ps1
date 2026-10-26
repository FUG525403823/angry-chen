# C15: 唯一发布入口。Output 是相对 client 根的目录，不能是 exe。
[CmdletBinding()]
param(
    [ValidateSet('Windows64')][string]$Target = 'Windows64',
    [string]$Output = 'Build/Windows64',
    [ValidateSet('il2cpp', 'mono', 'auto')][string]$Backend = 'auto'
)

$ErrorActionPreference = 'Stop'
$clientRoot = $PSScriptRoot
$repoRoot = Split-Path -Parent $clientRoot
$buildInfoPath = Join-Path $clientRoot 'Assets/Scripts/Core/BuildInfo.g.cs'
$originalBuildInfo = $null
$hadBuildInfo = Test-Path -LiteralPath $buildInfoPath -PathType Leaf
$buildInfoWritten = $false
$exitCode = 1
try {
    if ([string]::IsNullOrWhiteSpace($Output) -or $Output.TrimEnd('\', '/').EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output 必须是目录，不能是 .exe 路径'
    }
    $outputRoot = if ([IO.Path]::IsPathRooted($Output)) { [IO.Path]::GetFullPath($Output) } else { [IO.Path]::GetFullPath((Join-Path $clientRoot $Output)) }
    if (Test-Path -LiteralPath $outputRoot -PathType Leaf) { throw 'Output 必须是目录，不能是文件' }
    $editor = $env:AC_UNITY
    if (-not $editor) { $editor = (Get-ItemProperty -Path 'HKCU:\Environment' -Name 'AC_UNITY' -ErrorAction SilentlyContinue).AC_UNITY }
    if (-not $editor -or -not (Test-Path -LiteralPath $editor -PathType Leaf)) {
        $exitCode = 2
        throw "环境缺失：AC_UNITY 未指向可用的编辑器（当前值：'$editor'）"
    }
    $versionText = Get-Content -LiteralPath (Join-Path $clientRoot 'Assets/Scripts/Core/VersionInfo.cs') -Raw -Encoding UTF8
    $semver = ([regex]'SemanticVersion\s*=\s*"([^"]+)"').Match($versionText).Groups[1].Value
    $proto = ([regex]'ProtocolVersion\s*=\s*(\d+)').Match($versionText).Groups[1].Value
    if (-not $semver -or -not $proto) { throw '无法从 VersionInfo.cs 读出 semver/proto' }
    $sha7 = (& git -C $repoRoot rev-parse --short=7 HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $sha7) { throw '读不到 git commit' }
    $buildTime = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $stageDir = Join-Path $outputRoot "stage/$runId"
    $releaseRoot = Join-Path $outputRoot 'release'
    $releaseDir = Join-Path $releaseRoot $runId
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    $logDir = Join-Path $clientRoot "Logs/build/$runId"
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $logPath = Join-Path $logDir 'build.log'
    $errorPath = Join-Path $logDir 'build.stderr.log'
    $batchPath = Join-Path $logDir 'build.cmd'
    $il2cppAvailable = @(Get-ChildItem -Path (Join-Path (Split-Path -Parent $editor) 'Data/PlaybackEngines') -Recurse -Directory -Filter 'win64_il2cpp*' -ErrorAction SilentlyContinue).Count -gt 0
    $effectiveBackend = $Backend
    if ($Backend -eq 'auto' -or ($Backend -eq 'il2cpp' -and -not $il2cppAvailable)) {
        $effectiveBackend = if ($il2cppAvailable) { 'il2cpp' } else { 'mono' }
    }
    Write-Output "[build] backend=$effectiveBackend stage=$stageDir log=$logPath"
    if ($hadBuildInfo) { $originalBuildInfo = [IO.File]::ReadAllBytes($buildInfoPath) }
    $buildInfoWritten = $true
    [IO.File]::WriteAllText($buildInfoPath, @"
// Generated temporarily by client/build.ps1; restored in finally.
namespace Ac.Core
{
    internal static class BuildInfo
    {
        internal const string Commit = "$sha7";
        internal const string BuildTimeUtc = "$buildTime";
    }
}
"@, [Text.UTF8Encoding]::new($false))
    $engineArgs = '-batchmode -quit -nographics -projectPath "' + $clientRoot + '"' +
        ' -executeMethod Ac.Editor.BuildEntry.BuildWindows64 -logFile - -target ' + $Target + ' -output "' + $stageDir + '" -backend ' + $effectiveBackend
    # cmd waits for the GUI editor and preserves its exit code. Every run has its own logs.
    Set-Content -LiteralPath $batchPath -Encoding Default -Value ('@"' + $editor + '" ' + $engineArgs + ' > "' + $logPath + '" 2> "' + $errorPath + '"')
    & cmd.exe /d /c $batchPath
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Get-Content -LiteralPath $logPath, $errorPath -Encoding UTF8 -ErrorAction SilentlyContinue | Select-Object -Last 20 | ForEach-Object { Write-Output $_ }
        throw "编辑器失败 exit=$code log=$logPath"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $stageDir 'angry-chen.exe') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $stageDir 'angry-chen_Data') -PathType Container)) { throw 'stage 缺 angry-chen.exe 或 angry-chen_Data' }

    # Only this run's stage is eligible for shipping; real connection settings never ship.
    Get-ChildItem -LiteralPath $stageDir -Recurse -Force | Where-Object {
        $_.Name -eq 'server.txt' -or $_.Name -like '*_BurstDebugInformation_DoNotShip' -or
        $_.Name -like '*.zip' -or $_.Name -like '*.sha256' -or $_.Name -in @('latest.txt', 'manifest.json')
    } | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
        $candidate = [IO.Path]::GetFullPath($_.FullName)
        if (-not $candidate.StartsWith($stageDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw '拒绝删除 stage 外路径' }
        Remove-Item -LiteralPath $candidate -Force -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $clientRoot 'server.txt.example') -Destination (Join-Path $stageDir 'server.txt.example')
    $archiveName = "ac-client-$semver+$sha7-win64.zip"
    $archivePath = Join-Path $releaseDir $archiveName
    Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $stageDir | ForEach-Object { $_.FullName }) -DestinationPath $archivePath -CompressionLevel Optimal
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        if ('angry-chen.exe' -notin $names -or -not @($names | Where-Object { $_ -like 'angry-chen_Data/*' }).Count -or
            'server.txt.example' -notin $names -or @($names | Where-Object { ($_ -split '/') -contains 'server.txt' }).Count) { throw '归档完整性/连接配置检查失败' }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath ($archivePath + '.sha256') -Encoding ASCII -Value ($hash + '  ' + $archiveName)
    $manifestPath = Join-Path $releaseDir 'manifest.json'
    @{
        archive = $archiveName; sha256 = $hash; semver = $semver; commit = $sha7; proto = $proto
        backend = $effectiveBackend; builtUtc = $buildTime; runId = $runId
        versionLine = "ac-client $semver+$sha7 proto=$proto"
    } | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $liveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $hashLine = (Get-Content -LiteralPath ($archivePath + '.sha256') -Raw).Trim()
    if ($manifest.sha256 -ne $liveHash -or $hashLine -ne ($liveHash + '  ' + $archiveName)) { throw '清单与归档 sha256 不一致' }
    # All fallible build/package checks precede the atomic pointer replacement.
    $latestPath = Join-Path $releaseRoot 'latest.txt'
    $pendingPath = Join-Path $releaseDir 'latest.pending'
    Set-Content -LiteralPath $pendingPath -Encoding ASCII -Value ("$runId/$archiveName $hash")
    if ((Get-Content -LiteralPath $pendingPath -Raw).Trim() -ne "$runId/$archiveName $liveHash") { throw 'latest 校验失败' }
    $exitCode = 0
} catch {
    Write-Output ('[build] failed: ' + $_.Exception.Message)
} finally {
    if ($buildInfoWritten) {
        if ($hadBuildInfo) { [IO.File]::WriteAllBytes($buildInfoPath, $originalBuildInfo) }
        else {
            if ([IO.Path]::GetFullPath($buildInfoPath) -ne (Join-Path $clientRoot 'Assets/Scripts/Core/BuildInfo.g.cs')) { throw '拒绝删除非 BuildInfo 路径' }
            Remove-Item -LiteralPath $buildInfoPath -Force
        }
    }
}
if ($exitCode -eq 0) {
    try {
        if (Test-Path -LiteralPath $latestPath) { [IO.File]::Replace($pendingPath, $latestPath, (Join-Path $releaseDir 'latest.previous')) }
        else { [IO.File]::Move($pendingPath, $latestPath) }
    } catch {
        Write-Output ('[build] failed publishing latest: ' + $_.Exception.Message)
        exit 1
    }
    Write-Output "[build] archive=$archivePath"
    Write-Output "[build] sha256=$hash"
    Write-Output "BUILD OK $archiveName"
}
exit $exitCode
