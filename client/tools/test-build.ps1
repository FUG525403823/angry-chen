# Script-level release regression tests. No Unity editor is started.
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('ac-build-test-' + [guid]::NewGuid().ToString('N'))
$client = Join-Path $root 'client'
$oldEditor = $env:AC_UNITY
$oldMode = $env:AC_FAKE_MODE
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Run-Build([string]$output, [string]$mode = 'ok') {
    $env:AC_FAKE_MODE = $mode
    $lines = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $client 'build.ps1') -Output $output -Backend mono 2>&1
    $code = $LASTEXITCODE
    return @{ Code = $code; Text = ($lines -join "`n") }
}
try {
    New-Item -ItemType Directory -Path (Join-Path $client 'Assets/Scripts/Core') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../build.ps1') -Destination $client
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../server.txt.example') -Destination $client
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../Assets/Scripts/Core/VersionInfo.cs') -Destination (Join-Path $client 'Assets/Scripts/Core')
    $info = Join-Path $client 'Assets/Scripts/Core/BuildInfo.g.cs'
    $original = [byte[]](239,187,191,47,47,32,117,115,101,114,13,10)
    [IO.File]::WriteAllBytes($info, $original)
    & git -C $root init --quiet
    & git -C $root -c user.name=test -c user.email=test@example.invalid commit --allow-empty --quiet -m fixture
    Assert ($LASTEXITCODE -eq 0) 'fixture git init failed'
    $fake = Join-Path $root 'fake editor.cmd'
    $fakePs = Join-Path $root 'fake-editor.ps1'
    @'
param([string]$stage)
if ($env:AC_FAKE_MODE -eq 'fail') { exit 7 }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'angry-chen_Data'), (Join-Path $stage 'test_BurstDebugInformation_DoNotShip') | Out-Null
Set-Content (Join-Path $stage 'angry-chen.exe') 'fake executable'
if ($env:AC_FAKE_MODE -ne 'empty-data') { Set-Content (Join-Path $stage 'angry-chen_Data/payload') 'fake data' }
Set-Content (Join-Path $stage 'server.txt') 'secret.invalid:8788'
Set-Content (Join-Path $stage 'angry-chen_Data/server.txt') 'nested secret'
Set-Content (Join-Path $stage 'test_BurstDebugInformation_DoNotShip/debug') 'debug'
exit 0
'@ | Set-Content -LiteralPath $fakePs -Encoding UTF8
    @('@echo off', ':scan', 'if "%~1"=="-output" goto found', 'if "%~1"=="" exit /b 9', 'shift', 'goto scan', ':found', 'shift', ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $fakePs + '" -stage "%~1"'), 'exit /b %errorlevel%') | Set-Content -LiteralPath $fake -Encoding ASCII
    $env:AC_UNITY = $fake
    $out = 'Build/fixture output'
    $outputRoot = Join-Path $client $out
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    Set-Content (Join-Path $outputRoot 'stale.txt') 'must not ship'
    $result = Run-Build $out
    Assert ($result.Code -eq 0) ('success build failed: ' + $result.Text)
    Assert ($result.Text -match 'BUILD OK ac-client-') 'success marker missing'
    $latest = Join-Path $outputRoot 'release/latest.txt'
    $before = [IO.File]::ReadAllBytes($latest)
    $pointer, $hash = (Get-Content $latest) -split ' '
    $archive = Join-Path (Join-Path $outputRoot 'release') $pointer
    Assert ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hash) 'pointer hash mismatch'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        Assert ('server.txt.example' -in $names) 'example missing'
        Assert (-not ($names | Where-Object { $_ -match '(^|/)server.txt$|stale|DoNotShip|\.zip$|manifest.json|latest.txt' })) 'unwanted file shipped'
    } finally { $zip.Dispose() }
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($info)) -eq [Convert]::ToBase64String($original)) 'BuildInfo success restore mismatch'
    foreach ($mode in @('fail', 'empty-data')) {
        $result = Run-Build $out $mode
        Assert ($result.Code -eq 1) "$mode did not fail"
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($latest)) -eq [Convert]::ToBase64String($before)) "$mode damaged latest"
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($info)) -eq [Convert]::ToBase64String($original)) "$mode restore mismatch"
    }
    $result = Run-Build $out
    Assert ($result.Code -eq 0) ('rebuild failed: ' + $result.Text)
    Assert ((Get-Content $latest) -ne ([Text.Encoding]::ASCII.GetString($before)).Trim()) 'same-commit rebuild reused release'
    Assert (Test-Path -LiteralPath $archive) 'rebuild removed earlier archive'
    foreach ($bad in @('Build/bad.exe', (Join-Path $outputRoot 'stale.txt'))) {
        $result = Run-Build $bad
        Assert ($result.Code -eq 1) 'file/exe Output accepted'
    }
    # Missing generated file must remain missing after the build.
    Assert ($info -eq (Join-Path $client 'Assets/Scripts/Core/BuildInfo.g.cs')) 'unexpected cleanup path'
    Remove-Item -LiteralPath $info
    $result = Run-Build (Join-Path $root 'absolute output')
    Assert ($result.Code -eq 0) ('absolute output failed: ' + $result.Text)
    Assert (-not (Test-Path $info)) 'originally absent BuildInfo left behind'
    $env:AC_UNITY = Join-Path $root 'missing-editor.exe'
    $result = Run-Build $out
    Assert ($result.Code -eq 2) 'missing editor exit code mismatch'
} finally {
    $env:AC_UNITY = $oldEditor
    $env:AC_FAKE_MODE = $oldMode
    $resolved = [IO.Path]::GetFullPath($root)
    if ((Split-Path -Parent $resolved).TrimEnd('\') -ne ([IO.Path]::GetTempPath()).TrimEnd('\') -or (Split-Path -Leaf $resolved) -notlike 'ac-build-test-*') { throw 'unsafe fixture cleanup path' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
# Actions forwards LASTEXITCODE; expected failure probes must not leak their status.
$global:LASTEXITCODE = 0
Write-Output 'BUILD SCRIPT TEST OK (isolation, packaging, restoration, failure preservation, paths)'
