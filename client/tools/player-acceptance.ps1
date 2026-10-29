[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Player,
 [string]$ServerExe = "build/scratch/server-clean/ac_server.exe",
 [ValidateSet(2,4)][int]$Players = 2,
 [int]$UdpPort = 29788, [int]$HttpPort = 29787,
 [ValidateRange(0,1000)][int]$DelayMs = 0, [ValidateRange(0,100)][int]$LossPct = 0,
 [switch]$Reorder, [switch]$Duplicate, [switch]$Recovery,
 [ValidateRange(0,30)][int]$StableSeconds = 0,
 [string]$OutDir = "build/reports/player-acceptance"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$playerPath = (Resolve-Path $Player).Path
$serverPath = (Resolve-Path $ServerExe).Path
$out = [IO.Path]::GetFullPath((Join-Path $root $OutDir))
if (Test-Path $out) { throw "Evidence directory already exists: $out" }
New-Item -ItemType Directory -Path $out | Out-Null
$children = @()
$server = $null
$proxy = $null
$priorServer = $env:AC_SERVER
$code = 1
try {
 $server = Start-Process -FilePath $serverPath -WorkingDirectory $root -ArgumentList @("--serve", "--udp-port=$UdpPort", "--http-port=$HttpPort", "--data-dir=$out/data", "--minutes=5") -RedirectStandardOutput "$out/server.log" -RedirectStandardError "$out/server.err.log" -PassThru
 $null = $server.Handle
 $ready = $false
 for ($attempt=0; $attempt -lt 20; $attempt++) {
  if ($server.HasExited) { throw "Owned server exited before readiness" }
  try { $h = Invoke-RestMethod "http://127.0.0.1:$HttpPort/health" -TimeoutSec 1; if ($h.status -eq "ok") { $ready=$true; break } } catch {}
  Start-Sleep -Milliseconds 500
 }
 if (-not $ready) { throw "Owned server readiness timeout" }
 $clientPort = $UdpPort
 if ($DelayMs -gt 0 -or $LossPct -gt 0 -or $Reorder -or $Duplicate -or $Recovery) {
  $clientPort = $UdpPort + 2
  $node = (Get-Command node -ErrorAction Stop).Source
  $proxyArgs = @("`"$PSScriptRoot/udp-impairment.mjs`"", "port=$clientPort", "target=$UdpPort", "delay=$DelayMs", "loss=$LossPct", "reorder=$([int]$Reorder.IsPresent)", "duplicate=$([int]$Duplicate.IsPresent)", "`"report=$out/proxy.json`"", "outage=$(if($Recovery){5000}else{0})", "team=$Players", "`"trigger=$out`"")
  $proxy = Start-Process -FilePath $node -ArgumentList $proxyArgs -RedirectStandardOutput "$out/proxy.log" -RedirectStandardError "$out/proxy.err.log" -PassThru
  $null = $proxy.Handle
  $proxyReady = $false
  for ($i=0; $i -lt 20; $i++) {
   if ($proxy.HasExited) { throw "UDP proxy exited" }
   if ((Test-Path "$out/proxy.log") -and (Select-String -LiteralPath "$out/proxy.log" -Pattern "^UDP IMPAIRMENT READY " -Quiet)) { $proxyReady=$true; break }
   Start-Sleep -Milliseconds 100
  }
  if (-not $proxyReady) { throw "UDP proxy readiness timeout" }
 }
 $env:AC_SERVER = "127.0.0.1:$clientPort"
 for ($i=1; $i -le $Players; $i++) {
  $args = @("-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540", "-playerAcceptanceOut", "`"$out/peer-$i.json`"", "-playerAcceptanceName", "p0peer$i", "-playerAcceptanceTeam", "$Players", "-logFile", "`"$out/peer-$i.log`"")
  if ($Recovery) { $args += @("-playerAcceptanceRecovery", "1", "-playerAcceptanceStableSeconds", "$StableSeconds") }
  $child = Start-Process -FilePath $playerPath -WorkingDirectory (Split-Path -Parent $playerPath) -ArgumentList $args -PassThru
  $null = $child.Handle
  $children += $child
 }
 $deadline = [DateTime]::UtcNow.AddSeconds(120)
 foreach ($child in $children) {
  $remaining = [Math]::Max(1, [int]($deadline - [DateTime]::UtcNow).TotalMilliseconds)
  while (-not $child.WaitForExit([Math]::Min(1000, $remaining))) {
   if ($null -ne $proxy) { $proxy.Refresh(); if ($proxy.HasExited) { throw "UDP proxy exited during acceptance" } }
   $server.Refresh()
   if ($server.HasExited) { throw "Owned server exited during acceptance: $($server.ExitCode)" }
   $remaining = [Math]::Max(0, [int]($deadline - [DateTime]::UtcNow).TotalMilliseconds)
   if ($remaining -le 0) { throw "Independent player timed out" }
  }
  $child.Refresh()
  if ($null -eq $child.ExitCode -or $child.ExitCode -ne 0) { throw "Player $($child.Id) exit=$($child.ExitCode)" }
 }
 $results = @(for ($i=1; $i -le $Players; $i++) {
  if (Select-String -LiteralPath "$out/peer-$i.log" -Pattern "^[A-Za-z0-9_.]*(Exception|Error):" -Quiet) { throw "Unhandled player error peer-$i" }
  $j = Get-Content -Raw -LiteralPath "$out/peer-$i.json" | ConvertFrom-Json
  if ($j.pass -ne $true -or $j.sawTeam -ne $true -or $j.team -ne $Players -or $j.playerId -le 0 -or $j.session -le 0 -or $j.playingSnapshots -lt 20 -or $j.processId -ne $children[$i-1].Id) { throw "Invalid peer-$i evidence" }
  if ($Recovery -and ($j.recovered -ne $true -or $j.recoverySnapshots -lt 20 -or $j.identityPreserved -ne $true)) { throw "Missing recovery evidence peer-$i" }
  if ($StableSeconds -gt 0 -and ($j.stableSeconds -lt $StableSeconds -or $j.recoveryInterruptions -ne 0)) { throw "Unstable recovery peer-$i" }
  $j
 })
 foreach ($key in @("processId", "session", "playerId")) {
  if (@($results | Select-Object -ExpandProperty $key -Unique).Count -ne $Players) { throw "Duplicate $key" }
 }
 if ($null -ne $proxy) {
  $stats = Get-Content -Raw -LiteralPath "$out/proxy.json" | ConvertFrom-Json
  if ($Recovery -and ($stats.blackoutStarted -ne $true -or $stats.blackoutRecovered -ne $true -or $stats.blackoutDropped -le 0)) { throw "Missing actual blackout evidence" }
  if ($stats.peers -ne $Players -or $stats.forwarded -le 0 -or $stats.errors -ne 0 -or ($LossPct -gt 0 -and $stats.dropped -le 0) -or ($Reorder -and $stats.reordered -le 0) -or ($Duplicate -and $stats.duplicated -le 0)) { throw "Invalid UDP impairment evidence" }
 }
 $results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 "$out/results.json"
 Write-Output "INDEPENDENT PLAYER ACCEPTANCE OK players=$Players"
 $code = 0
} catch {
 Write-Output "INDEPENDENT PLAYER ACCEPTANCE FAIL $($_.Exception.Message)"
 if ($null -ne $server) { $server.Refresh(); if ($server.HasExited) { Write-Output "Owned server exit=$($server.ExitCode)" } }
}
finally {
 if ($null -eq $priorServer) { Remove-Item Env:AC_SERVER -ErrorAction SilentlyContinue } else { $env:AC_SERVER=$priorServer }
 foreach ($owned in @($children) + @($proxy) + @($server)) { if ($null -ne $owned) { if (-not $owned.HasExited) { $owned.Kill(); $owned.WaitForExit() }; $owned.Dispose() } }
}
exit $code
