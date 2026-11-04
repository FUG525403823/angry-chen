// Offline guardrails only; does not claim real-player acceptance or human fun.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const driver=fs.readFileSync(path.join(root,'client/Assets/Scripts/Boot/ExpeditionAcceptance.cs'),'utf8');
const runner=fs.readFileSync(path.join(root,'client/tools/expedition-acceptance.ps1'),'utf8');
const proxy=fs.readFileSync(path.join(root,'client/tools/udp-impairment.mjs'),'utf8');
assert.match(driver, /-expeditionAcceptanceOut/);
assert.doesNotMatch(driver, /-playerAcceptanceOut/);
assert.match(driver, /loop\.RequestExpedition\(op, choice\)/);
assert.match(driver, /sampler\.AllowBackgroundInjectedInput = true/);
assert.match(driver, /state == null[\s\S]*?Request\(loop, 1\)/, 'Classic lobby must not depend on expedition first state');
assert.match(driver, /state\.Result == 1 && state\.Node == 3 && _nodeBits == 7 && _progressBits == 7/);
for(const gate of ['_choices >= 3','_tools > 0','_kills > 0','_triggers > 0','loop.CommandsSent > 1000']) assert.ok(driver.includes(gate),gate);
for(const field of ['OfferId','RewardDeadlineMs','LastRequestId','LastResult','Choices','CardMask']) assert.ok(driver.includes(`state.${field}`)||driver.includes(`state.Players[j].${field}`),field);
assert.match(driver, /Math\.Atan2\(dx, dz\)/);
assert.match(driver, /Convert\.ToBase64String\(ExpeditionCodec\.EncodeState\(s\)\)/);
assert.doesNotMatch(driver, /Sampler\.Flush\(|sampler\.Flush\(/, 'render driver must preserve production 30Hz uplink');
assert.match(driver, /_pendingOp == 5 && state\.LastResult == 8/);
assert.match(driver, /_nextAction = now \+ 1/);
assert.match(driver, /BarnBlocks\(x, z, e\.XCm/);
assert.match(driver, /BarnBlocks\(x, z, goalX, goalZ, 1\.2\)/);
assert.match(driver, /local\.YCm \/ 100\.0 \+ 1\.6/);
assert.match(driver, /target\.YCm \/ 100\.0 \+ \.90/);
assert.doesNotMatch(driver, /\.hp\s*=|\.pos\s*=|timeScale\s*=|stepWorld\(|beginEncounter\(/i);
assert.match(runner, /ValidateSet\(1,2,4\)/);
assert.match(runner, /\[ValidateRange\(640,7680\)\]\[int\]\$WindowWidth = 960/);
assert.match(runner, /\[ValidateRange\(360,4320\)\]\[int\]\$WindowHeight = 540/);
assert.match(runner, /'-screen-width', "\$WindowWidth", '-screen-height', "\$WindowHeight"/);
assert.match(runner, /requestedWindowWidth=\$WindowWidth; requestedWindowHeight=\$WindowHeight/);
assert.match(runner, /ValidateSet\('combat','reward'\)/);
assert.match(runner, /-expeditionAcceptanceRecoveryStage/);
assert.match(driver, /reward-recovery-deadline-expired/);
assert.match(driver, /reward-recovery-stable/);
assert.match(driver, /!_rewardRecovery && !_armed/);
assert.match(driver, /rewardRecovered = _rewardRecovery && _recovered/);
assert.match(runner, /\$delay=50; \$loss=5/);
assert.match(runner, /\$delay=100; \$loss=10/);
assert.match(runner, /'reorder=1', 'duplicate=1'/);
assert.match(runner, /'outage=5000'/);
assert.match(runner, /StableSeconds', '15'/);
assert.match(runner, /peer-exits\.json/);
assert.match(runner, /forcedTermination\s*=\$forced/);
assert.match(runner, /server-at-failure\.json/);
assert.match(runner, /processStartUtc/);
assert.match(runner, /processEndUtc/);
assert.match(runner, /processes\.json/);
assert.match(runner, /source-hashes\.json/);
assert.match(runner, /failure\.txt/);
assert.match(runner, /humanFunVerified=\$false/);
assert.doesNotMatch(runner, /Remove-Item.*\$out/);
assert.match(proxy, /peer-\$\{i\+1\}\.json\.ready/);
// Production direction convention: yaw 0=+Z, +pi/2=+X, positive pitch points up.
for(const [dx,dy,dz] of [[1,0,0],[0,0,1],[-1,0,0],[2,-.7,3]]) {
 const yaw=Math.atan2(dx,dz), pitch=Math.atan2(dy,Math.hypot(dx,dz));
 const v=[Math.sin(yaw)*Math.cos(pitch), Math.sin(pitch), Math.cos(yaw)*Math.cos(pitch)];
 const length=Math.hypot(dx,dy,dz);
 for(const [i,n] of [dx,dy,dz].entries()) assert.ok(Math.abs(v[i]-n/length)<1e-10);
}
// Execute the actual runner lifecycle helpers with ordinary child shells, never games/server.
// This catches exit-code fabrication, forced/natural confusion, and missing original timestamps.
if (process.platform === 'win32') {
 const script=String.raw`$ErrorActionPreference='Stop';$e=$null;$t=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path (Get-Location) 'client/tools/expedition-acceptance.ps1'),[ref]$t,[ref]$e);if($e.Count){throw 'Runner AST parse failed'};$processRecords=@{};$ast.FindAll({param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst]},$true)|ForEach-Object{Invoke-Expression $_.Extent.Text};foreach($forced in @($false,$true)){$command=if($forced){'Start-Sleep -Seconds 60'}else{'exit 7'};$p=Start-Process powershell.exe -ArgumentList @('-NoProfile','-Command',$command) -PassThru;try{$null=$p.Handle;Remember-ProcessStart $p server;if(-not $forced){$p.WaitForExit()};$r=Finish-ProcessRecord $p;if($r.forcedTermination -ne $forced -or -not $r.processStartUtc -or -not $r.processEndUtc -or ([DateTime]$r.processEndUtc -lt [DateTime]$r.processStartUtc)){throw 'Lifecycle metadata incorrect'};if(-not $forced -and $r.exitCode -ne 7){throw 'Natural exit7 lost'};if($forced -and $r.exitCode -eq 0){throw 'Kill misreported as success'};$r|ConvertTo-Json -Compress}finally{if(-not $p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}}`;
 const check=spawnSync('powershell.exe',['-NoProfile','-Command',script],{cwd:root,encoding:'utf8',timeout:15000});
 assert.equal(check.status,0,`Process lifecycle semantic test failed: ${check.stdout}\n${check.stderr}`);
 console.log(check.stdout.trim());
}
console.log('EXPEDITION ACCEPTANCE OFFLINE GUARDRAILS OK (real players and human fun NOT verified)');
