<#
  服务端唯一构建入口（S01 §5.3）：
    powershell -NoProfile -File server/build.ps1 -Config Release
  优先用 vswhere 定位 VS 自带的 cmake + ninja（要求 cmake >= 3.28）；配置失败（工具链不可用）才回退
  mingw g++ 直编。CMake 配置成功但编译失败属于真实错误，直接非零退出，不用兜底掩盖。
  成功末行固定为 [build] ok ac_server.exe。本文件必须保存为 UTF-8 with BOM（Windows PowerShell 5.1 读盘依赖它）。
#>
[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$serverRoot = $PSScriptRoot
$buildDir = Join-Path $serverRoot 'build'
$srcDir = Join-Path $serverRoot 'src'
$testsDir = Join-Path $serverRoot 'tests'
$exeSuffix = '.exe'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

function Write-Build([string]$message) { Write-Host "[build] $message" }
function Fail-Build([string]$message) {
  Write-Host "[build] FAIL $message" -ForegroundColor Red
  exit 1
}

function Get-VsBundledTool([string]$relativePath) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
  $vsPath = & $vswhere -latest -products * -property installationPath
  if (-not $vsPath) { return $null }
  $candidate = Join-Path $vsPath.Trim() $relativePath
  if (Test-Path -LiteralPath $candidate) { return $candidate }
  return $null
}

function Get-PathTool([string]$name) {
  $command = Get-Command $name -ErrorAction SilentlyContinue
  if ($command) { return $command.Source }
  return $null
}

function Get-CMakeVersion([string]$cmake) {
  $firstLine = & $cmake --version | Select-Object -First 1
  if ($firstLine -match '(\d+)\.(\d+)') { return [version]"$($Matches[1]).$($Matches[2])" }
  return [version]'0.0'
}

$cmake = Get-VsBundledTool 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not $cmake) { $cmake = Get-PathTool 'cmake' }
$ninja = Get-VsBundledTool 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not $ninja) { $ninja = Get-PathTool 'ninja' }

$toolchain = ''
if ($cmake) {
  $cmakeVersion = Get-CMakeVersion $cmake
  if ($cmakeVersion -ge [version]'3.28') {
    Write-Build "cmake $cmakeVersion -> $cmake"
    if ($ninja) { Write-Build "ninja -> $ninja" }
    $configureArgs = @('-S', $serverRoot, '-B', $buildDir, '-G', 'Ninja',
      "-DCMAKE_BUILD_TYPE=$Config", '-DAC_WERROR=ON')
    if ($ninja) { $configureArgs += "-DCMAKE_MAKE_PROGRAM=$ninja" }
    & $cmake @configureArgs
    if ($LASTEXITCODE -eq 0) {
      & $cmake --build $buildDir
      if ($LASTEXITCODE -ne 0) { Fail-Build 'cmake 构建失败（配置已成功说明工具链可用，不做兜底掩盖）' }
      $toolchain = "cmake $cmakeVersion + ninja + $((Get-PathTool 'g++'))"
    } else {
      Write-Warning '[build] cmake 配置失败（工具链不可用？），回退到 mingw g++ 直编'
    }
  } else {
    Write-Warning "[build] cmake $cmakeVersion 低于 3.28，回退到 mingw g++ 直编"
  }
} else {
  Write-Warning '[build] 未找到 cmake，改用 mingw g++ 直编'
}

if (-not $toolchain) {
  $gxx = Get-PathTool 'g++'
  if (-not $gxx) { Fail-Build '既没有 cmake（>= 3.28）也没有 g++，无法构建' }
  Write-Build "g++ -> $gxx"
  $optimize = if ($Config -eq 'Release') { @('-O2', '-DNDEBUG') } else { @('-O0', '-g') }
  $commonFlags = @('-std=c++20') + $optimize + @('-ffp-contract=off', '-fno-fast-math',
    '-Wall', '-Wextra', '-Werror', '-I', $srcDir)
  $mainSource = Join-Path $srcDir 'main.cpp'
  $librarySources = Get-ChildItem -LiteralPath $srcDir -Filter *.cpp -Recurse |
    Where-Object { $_.FullName -ne $mainSource } |
    ForEach-Object { $_.FullName }

  & $gxx @commonFlags $mainSource $librarySources '-o' (Join-Path $buildDir "ac_server$exeSuffix")
  if ($LASTEXITCODE -ne 0) { Fail-Build 'g++ 直编 ac_server 失败' }

  & $gxx @commonFlags '-I' $testsDir (Join-Path $testsDir 'main_test.cpp') $librarySources '-o' (Join-Path $buildDir "ac_tests$exeSuffix")
  if ($LASTEXITCODE -ne 0) { Fail-Build 'g++ 直编 ac_tests 失败' }

  $toolchain = "mingw g++ $(& $gxx -dumpversion)"
}

$serverExe = Join-Path $buildDir "ac_server$exeSuffix"
$testsExe = Join-Path $buildDir "ac_tests$exeSuffix"
foreach ($artifact in @($serverExe, $testsExe)) {
  if (-not (Test-Path -LiteralPath $artifact)) { Fail-Build "缺少产物 $artifact" }
}

Write-Build "toolchain: $toolchain"
Write-Build "ac_server$exeSuffix -> $serverExe"
Write-Build "ac_tests$exeSuffix -> $testsExe"
Write-Build "ok ac_server$exeSuffix"
