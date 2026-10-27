[CmdletBinding()]
param([string]$Unity = $env:AC_UNITY)
$ErrorActionPreference = "Stop"
if (-not $Unity) { $Unity = (Get-ItemProperty -Path "HKCU:\Environment" -Name AC_UNITY -ErrorAction SilentlyContinue).AC_UNITY }
if (-not $Unity -or -not (Test-Path -LiteralPath $Unity)) { Write-Error "AC_UNITY is unavailable"; exit 2 }
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$project = Join-Path $repo "client"
$log = Join-Path $project "Logs/playmode-smoke.log"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
$child = Start-Process -FilePath $Unity -ArgumentList @("-batchmode", "-projectPath", "`"$project`"", "-executeMethod", "Ac.Tests.PlayModeSmoke.Run", "-logFile", "`"$log`"") -PassThru
$null = $child.Handle
if (-not $child.WaitForExit(180000)) { $child.Kill(); Write-Error "PlayMode smoke timed out"; exit 1 }
$child.Refresh()
$code = $child.ExitCode
if ($null -eq $code -or $code -ne 0) { Write-Error "PlayMode process failed: $code"; exit 1 }
$lines = Get-Content -LiteralPath $log
$ok = @($lines | Where-Object { $_ -match "^PLAYMODE SMOKE OK frames=[0-9]+$" })
$bad = @($lines | Where-Object { $_ -match "PLAYMODE SMOKE FAIL|error CS[0-9]+|^.*Exception:" })
if ($ok.Count -ne 1 -or $bad.Count -ne 0) { Write-Error "Missing or invalid PlayMode smoke evidence"; exit 1 }
$ok
exit 0
