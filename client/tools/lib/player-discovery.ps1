# Resolve only the published run; abandoned stages and legacy root executables are never candidates.
function Resolve-PublishedPlayer {
    param([string]$Project, [string]$Player = '')
    if (-not [string]::IsNullOrWhiteSpace($Player)) { return $Player }
    $root = Join-Path $Project 'Build/Windows64'
    $latest = Join-Path $root 'release/latest.txt'
    if (-not (Test-Path -LiteralPath $latest -PathType Leaf)) { return $null }
    try {
        $pointer = (Get-Content -LiteralPath $latest -Raw -Encoding UTF8).Trim()
        if ($pointer -notmatch '^([A-Za-z0-9][A-Za-z0-9_-]*)/([^/\\\s]+\.zip) ([a-fA-F0-9]{64})$') { return $null }
        $runId = $Matches[1]
        $archiveName = $Matches[2]
        $hash = $Matches[3]
        $archive = Join-Path $root ('release/' + $runId + '/' + $archiveName)
        if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { return $null }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $hash) { return $null }
        $stage = Join-Path $root ('stage/' + $runId)
        $exe = Join-Path $stage 'angry-chen.exe'
        if ((Test-Path -LiteralPath $exe -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $stage 'angry-chen_Data') -PathType Container)) {
            return (Get-Item -LiteralPath $exe).FullName
        }
    } catch { return $null }
    return $null
}
