# Read-only tool discovery; no PATH mutation or installation.
function Resolve-P0CMake {
    if ($env:AC_CMAKE) {
        if (-not (Test-Path -LiteralPath $env:AC_CMAKE -PathType Leaf)) { throw 'AC_CMAKE points to a missing executable' }
        return (Resolve-Path -LiteralPath $env:AC_CMAKE).Path
    }
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidates = @()
    $vswhere = @("${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe", "$env:ProgramFiles/Microsoft Visual Studio/Installer/vswhere.exe")
    $vswhere += @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { Join-Path $_.Root 'tools/VS2022/Installer/vswhere.exe' })
    foreach ($locator in $vswhere) {
        if (-not (Test-Path -LiteralPath $locator -PathType Leaf)) { continue }
        $found = & $locator -latest -products '*' -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if ($LASTEXITCODE -eq 0) { $candidates += @($found) }
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($base) { $candidates += @(Get-ChildItem -Path "$base/Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" -ErrorAction SilentlyContinue | ForEach-Object FullName) }
    }
    $candidates += @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { Join-Path $_.Root 'tools/VS2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' })
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'CMake unavailable: set AC_CMAKE to its real executable or install Visual Studio CMake tools'
}
