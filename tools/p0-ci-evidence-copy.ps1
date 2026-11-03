# Publish sanitized copies only; retain unmodified engine evidence locally for diagnosis.
[CmdletBinding()]
param([string]$Destination = 'build/ci-artifacts/p0-engine', [string[]]$SourceRoots = @('build/reports/engine', 'build/scratch/p0-engine/release/client/Logs', 'build/scratch/p0-engine/development/client/Logs'))
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Destination) { throw 'Refusing to overwrite prior sanitized evidence' }
$roots = $SourceRoots
$secrets = @($env:GITHUB_TOKEN, $env:GH_TOKEN, $env:ACTIONS_RUNTIME_TOKEN) | Where-Object { $_ -and $_.Length -ge 8 }
$count = 0
foreach ($root in $roots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    $base = (Resolve-Path -LiteralPath $root).Path
    foreach ($file in Get-ChildItem -LiteralPath $base -File -Recurse) {
        if ($file.Extension -notin @('.log', '.txt', '.json', '.buildreport')) { continue }
        $relative = $file.FullName.Substring($base.Length).TrimStart('\', '/')
        $target = Join-Path $Destination (Join-Path $root $relative)
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        if ($file.Extension -eq '.buildreport') {
            # Unity's serialized report is retained byte-for-byte; do not corrupt its binary schema.
            Copy-Item -LiteralPath $file.FullName -Destination $target
        } else {
            $text = [IO.File]::ReadAllText($file.FullName)
            # License serials, machine/session identifiers and licensing diagnostics are private.
            $text = [regex]::Replace($text, '(?im)^.*(?:\[Licensing::|serial\s*number|license\s*serial|machine\s*id|machineid|session\s*id|sessionid).*(?:\r?\n|$)', '[REDACTED_PRIVATE_LICENSE_LINE]' + "`n")
            foreach ($secret in $secrets) { $text = $text.Replace($secret, '[REDACTED]') }
            $text = [regex]::Replace($text, '(?i)\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})', '[REDACTED_GITHUB_TOKEN]')
            $text = [regex]::Replace($text, '(?i)(authorization\s*[:=]\s*)(?:Bearer\s+|Basic\s+)?[^\s"'']+', '$1[REDACTED]')
            $text = [regex]::Replace($text, '(?i)([?&](?:access_token|token|licenseKey|serial)=)[^&\s"'']+', '$1[REDACTED]')
            [IO.File]::WriteAllText($target, $text, [Text.UTF8Encoding]::new($false))
        }
        $count++
    }
}
if ($count -eq 0) { throw 'No engine evidence exists to sanitize/upload' }
Write-Output "P0 SANITIZED EVIDENCE files=$count"
