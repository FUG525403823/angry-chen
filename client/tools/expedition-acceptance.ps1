[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Player,
 [string]$ServerExe = "build/scratch/server-clean/ac_server.exe",
 [ValidateSet(1,2,4)][int]$Players = 1,
 [ValidateSet('hunter','breach')][string]$SoloBuild = 'hunter',
 [int]$UdpPort = 29888, [int]$HttpPort = 29887,
 [switch]$Impaired,
 [ValidateSet('combat','reward')][string]$RecoveryStage = 'combat',
 [ValidateRange(600,1800)][int]$TimeoutSeconds = 900,
 [string]$OutDir = 'build/reports/expedition-acceptance'
)
# Independent P1 runner. No Unity/build invocation, no P0 behavior changes, failures never deleted.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$playerPath = (Resolve-Path $Player).Path
$serverPath = (Resolve-Path $ServerExe).Path
if ($Impaired -and $Players -eq 1) { throw 'Impaired matrix is defined only for 2/4 peers' }
$round = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$out = Join-Path ([IO.Path]::GetFullPath((Join-Path $root $OutDir))) $round
New-Item -ItemType Directory -Path $out | Out-Null
$children = @(); $server = $null; $proxy = $null; $code = 1
$delay = 0; $loss = 0
if ($Impaired) { if ($Players -eq 2) { $delay=50; $loss=5 } else { $delay=100; $loss=10 } }
$exitRecords = @()
$processRecords = @{}
function Remember-ProcessStart($process, [string]$role) {
 $processRecords[$process.Id] = [ordered]@{role=$role;processId=$process.Id;processStartUtc=$process.StartTime.ToUniversalTime().ToString('o');processEndUtc=$null;exitCode=$null;forcedTermination=$false}
}
function Finish-ProcessRecord($process) {
 $process.Refresh(); $forced = -not $process.HasExited
 if($forced){$process.Kill()}
 $process.WaitForExit(); $process.Refresh()
 $record=$processRecords[$process.Id]
 $record.forcedTermination=$forced; $record.exitCode=$process.ExitCode
 $record.processEndUtc=$process.ExitTime.ToUniversalTime().ToString('o')
 return $record
}
try {
 $source = @(git -C $root ls-files --cached --others --exclude-standard client/Assets/Scripts server/src docs/expedition-p1-content.txt)
 $hashes = @(foreach ($relative in $source) {
  $path = Join-Path $root $relative
  if (Test-Path -LiteralPath $path -PathType Leaf) { [ordered]@{ path=$relative; sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash } }
 })
 $hashes | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$out/source-hashes.json"
 $playerRoot=Split-Path -Parent $playerPath
 @(Get-ChildItem -LiteralPath $playerRoot -Recurse -File | ForEach-Object {
  [ordered]@{path=$_.FullName.Substring($playerRoot.Length+1);bytes=$_.Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash}
 }) | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$out/player-tree-hashes.json"
 [ordered]@{ round=$round; players=$Players; impaired=[bool]$Impaired; recoveryStage=$(if($Impaired){$RecoveryStage}else{'none'}); delayMs=$delay; lossPct=$loss; outageMs=$(if($Impaired){5000}else{0}); stableSeconds=15; timeoutSeconds=$TimeoutSeconds; humanFunVerified=$false; dirtyProjection=$true; head=(git -C $root rev-parse HEAD); status=@(git -C $root status --porcelain); player=$playerPath; playerSha256=(Get-FileHash $playerPath).Hash; server=$serverPath; serverSha256=(Get-FileHash $serverPath).Hash; contentSha256=(Get-FileHash (Join-Path $root 'docs/expedition-p1-content.txt')).Hash } | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$out/manifest.json"
 Copy-Item -LiteralPath (Join-Path $root 'docs/expedition-p1-content.txt') -Destination "$out/content.txt"
 $server = Start-Process -FilePath $serverPath -WorkingDirectory $root -ArgumentList @('--serve', "--udp-port=$UdpPort", "--http-port=$HttpPort", "`"--data-dir=$out/data`"", '--minutes=35') -RedirectStandardOutput "$out/server.log" -RedirectStandardError "$out/server.err.log" -PassThru
 $null = $server.Handle
 Remember-ProcessStart $server 'server'
 $ready=$false
 for ($i=0; $i -lt 30; $i++) {
  if ($server.HasExited) { throw 'Owned server exited before readiness' }
  try { $h=Invoke-RestMethod "http://127.0.0.1:$HttpPort/health" -TimeoutSec 1; if($h.status -eq 'ok'){$ready=$true;break} } catch {}
  Start-Sleep -Milliseconds 500
 }
 if (-not $ready) { throw 'Owned server readiness timeout' }
 $clientPort=$UdpPort
 if ($Impaired) {
  $clientPort=$UdpPort+2
  $node=(Get-Command node -ErrorAction Stop).Source
  $proxy = Start-Process -FilePath $node -WorkingDirectory $root -ArgumentList @("`"$PSScriptRoot/udp-impairment.mjs`"", "port=$clientPort", "target=$UdpPort", "delay=$delay", "loss=$loss", 'reorder=1', 'duplicate=1', "`"report=$out/proxy.json`"", 'outage=5000', "team=$Players", 'trace=1', "`"trigger=$out`"") -RedirectStandardOutput "$out/proxy.log" -RedirectStandardError "$out/proxy.err.log" -PassThru
  $null=$proxy.Handle; Remember-ProcessStart $proxy 'proxy'; $ready=$false
  for($i=0;$i -lt 30;$i++) {
   if($proxy.HasExited){throw 'UDP proxy exited'}
   if((Test-Path "$out/proxy.log") -and (Select-String -LiteralPath "$out/proxy.log" -Pattern '^UDP IMPAIRMENT READY ' -Quiet)){$ready=$true;break}
   Start-Sleep -Milliseconds 100
  }
  if(-not $ready){throw 'UDP proxy readiness timeout'}
 }
 for ($i=1;$i -le $Players;$i++) {
  $build=if($Players -eq 1){$SoloBuild}elseif($i % 2 -eq 1){'hunter'}else{'breach'}
  $arguments=@('-acserver', "127.0.0.1:$clientPort", '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540', '-expeditionAcceptanceOut', "`"$out/peer-$i.json`"", '-expeditionAcceptanceName', "p1peer$i", '-expeditionAcceptanceTeam', "$Players", '-expeditionAcceptanceBuild', $build, '-expeditionAcceptanceTimeout', "$TimeoutSeconds", '-expeditionAcceptanceRecovery', "$([int]$Impaired.IsPresent)", '-expeditionAcceptanceStableSeconds', '15', '-expeditionAcceptanceRecoveryStage', $RecoveryStage, '-logFile', "`"$out/peer-$i.log`"")
  $child=Start-Process -FilePath $playerPath -WorkingDirectory (Split-Path -Parent $playerPath) -ArgumentList $arguments -PassThru
  $null=$child.Handle; $children += $child
  Remember-ProcessStart $child "peer-$i"
 }
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds+30)
 # Do not fail fast on a single player's result: retain every peer's actual outcome.
 while (@($children | Where-Object {-not $_.HasExited}).Count -gt 0) {
  if([DateTime]::UtcNow -ge $deadline){throw 'Independent players timed out'}
  $server.Refresh();if($server.HasExited){throw 'Owned server exited during acceptance'}
  if($null -ne $proxy){$proxy.Refresh();if($proxy.HasExited){throw 'Owned proxy exited during acceptance'}}
  Start-Sleep -Milliseconds 500
  foreach($child in $children){$child.Refresh()}
 }
 $results=@(for($i=1;$i -le $Players;$i++) {
  $child=$children[$i-1];$child.WaitForExit();$child.Refresh()
  $j=Get-Content -Raw -LiteralPath "$out/peer-$i.json" | ConvertFrom-Json
  if($child.ExitCode -ne 0 -or $j.pass -ne $true -or $j.processId -ne $child.Id -or $j.team -ne $Players -or $j.nodes -ne 7 -or $j.progressNodes -ne 7 -or $j.acceptedChoices -lt 3 -or $j.acceptedTools -lt 1 -or $j.kills -lt 1 -or $j.triggers -lt 1 -or $j.runId -eq 0){throw "Invalid peer-$i outcome exit=$($child.ExitCode)"}
  if($Impaired -and $j.recoveryStage -ne $RecoveryStage){throw "Recovery stage mismatch peer-$i"}
  if($Impaired -and $RecoveryStage -eq 'reward' -and ($j.rewardRecovered -ne $true -or $j.rewardStable -ne $true)){throw "Reward offer recovery gate failed peer-$i"}
  if($Impaired -and $RecoveryStage -eq 'combat' -and $j.combatRecovered -ne $true){throw "Combat recovery gate failed peer-$i"}
  if($Impaired -and ($j.recovered -ne $true -or $j.identityPreserved -ne $true -or $j.recoveryInterruptions -ne 0 -or $j.stableSeconds -lt 15)){throw "Recovery gate failed peer-$i"}
  if(Select-String -LiteralPath "$out/peer-$i.log" -Pattern '^[A-Za-z0-9_.]*(Exception|Error):' -Quiet){throw "Unhandled error peer-$i"}
  $j
 })
 foreach($key in @('processId','session','playerId')){if(@($results | Select-Object -ExpandProperty $key -Unique).Count -ne $Players){throw "Duplicate $key"}}
 if(@($results | Select-Object -ExpandProperty runId -Unique).Count -ne 1){throw 'Peers disagree on authoritative run'}
 if($Impaired){
  $stats=Get-Content -Raw "$out/proxy.json" | ConvertFrom-Json
  if($stats.peers -ne $Players -or $stats.errors -ne 0 -or $stats.forwarded -le 0 -or $stats.dropped -le 0 -or $stats.reordered -le 0 -or $stats.duplicated -le 0 -or $stats.blackoutStarted -ne $true -or $stats.blackoutRecovered -ne $true -or $stats.blackoutDropped -le 0){throw 'Missing actual UDP impairment evidence'}
 }
 $results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 "$out/results.json"
 $code=0
} catch {
 if($null -ne $server){
  $server.Refresh()
  $serverAtFailure=[ordered]@{observedAtUtc=[DateTime]::UtcNow.ToString('o');processId=$server.Id;hasExited=$server.HasExited;exitCode=$null;processStartUtc=$server.StartTime.ToUniversalTime().ToString('o');processEndUtc=$null}
  if($server.HasExited){$server.WaitForExit();$serverAtFailure.exitCode=$server.ExitCode;$serverAtFailure.processEndUtc=$server.ExitTime.ToUniversalTime().ToString('o')}
  $serverAtFailure | ConvertTo-Json | Set-Content -Encoding UTF8 "$out/server-at-failure.json"
 }
 $_.Exception.ToString() | Set-Content -Encoding UTF8 "$out/failure.txt"
 Write-Output "EXPEDITION ACCEPTANCE FAIL $($_.Exception.Message) evidence=$out"
} finally {
 foreach($child in $children){
  $exitRecords += Finish-ProcessRecord $child
 }
 $exitRecords | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$out/peer-exits.json"
 foreach($owned in @($proxy)+@($server)){if($null -ne $owned){
  $record=Finish-ProcessRecord $owned
  $record | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$out/$($record.role)-exit.json"
 }}
 @($processRecords.Values) | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$out/processes.json"
 foreach($owned in @($children)+@($proxy)+@($server)){if($null -ne $owned){$owned.Dispose()}}
 [ordered]@{pass=($code -eq 0);humanFunVerified=$false;evidence=$out} | ConvertTo-Json | Set-Content -Encoding UTF8 "$out/outcome.json"
}
if($code -eq 0){Write-Output "EXPEDITION ACCEPTANCE TECHNICAL OK players=$Players evidence=$out human-fun=PENDING"}
exit $code
