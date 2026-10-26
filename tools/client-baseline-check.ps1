[CmdletBinding()]
param([ValidateSet('Compare','HEAD','Worktree')][string]$Mode='Compare',[string]$RoslynDirectory='D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$scratch=Join-Path $root 'build/scratch/client-baseline'
New-Item -ItemType Directory -Force $scratch | Out-Null
if($Mode -eq 'Compare') {
 foreach($side in @('HEAD','Worktree')) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Mode $side -RoslynDirectory $RoslynDirectory
  if($LASTEXITCODE -ne 0){throw "$side harness failed to execute"}
 }
 $head=Get-Content (Join-Path $scratch 'HEAD/results.json') -Raw | ConvertFrom-Json
 $work=Get-Content (Join-Path $scratch 'Worktree/results.json') -Raw | ConvertFrom-Json
 for($i=0;$i -lt $head.tests.Count;$i++) {
  if($head.tests[$i].name -ne $work.tests[$i].name -or $head.tests[$i].error -ne $work.tests[$i].error){throw 'Baseline verdict differs'}
  $expected=@('expected=42 actual=0','expected=3 actual=5')[$i]
  if($head.tests[$i].passed -or $head.tests[$i].error -ne $expected){throw "Expected existing failure changed: $expected"}
 }
 Write-Output 'HEAD and Worktree reproduce identical existing failures (comparison PASS; tests remain RED).'
 return
}
if([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object {$_.GetName().Name -match '^UnityEngine'}){throw 'Unity assemblies forbidden'}
$dest=Join-Path $scratch $Mode
New-Item -ItemType Directory -Force $dest | Out-Null
$headId=git -C $root rev-parse HEAD
function Read-Source([string]$relative) {
 if($Mode -eq 'HEAD') {
  $lines=git -C $root show "${headId}:$relative"
  if($LASTEXITCODE -ne 0){throw "git show failed: $relative"}
  return ($lines -join "`n")
 }
 return [IO.File]::ReadAllText((Join-Path $root $relative))
}
$files=@('Boot/GameLoop.cs','View/EntityViews.cs','View/Interpolation.cs','View/ErrorSmoother.cs','Core/InputSampler.cs','Core/MiniJson.cs','Core/FrameProfiler.cs','Core/CombatFlags.cs','Core/SettingsStore.cs')
$files+=@(git -C $root ls-tree -r --name-only $headId -- client/Assets/Scripts/Sim | Where-Object {$_ -match '\.cs$' -and $_ -notmatch 'FixtureLoader.cs'} | ForEach-Object {$_.Substring('client/Assets/Scripts/'.Length)})
$files+=@('CommandCodec','EventCodec','EventIdTracker','LocalIdentity','MatchStateCodec','PacketReader','PacketWriter','SnapshotCodec','UpgradeCodec' | ForEach-Object {"Net/$_.cs"})
$files+=@('Hud','AmmoCounter','RageBar','Crosshair','DownedOverlay','RevivePrompt','WaveBanner','KillFeed','UiThrottle' | ForEach-Object {"UI/$_.cs"})
$usings=@('using Debug = UnityEngine.Debug;','using System;','using Ac.Boot;','using Ac.Core;','using Ac.Net;','using Ac.Sim;','using Ac.UI;','using Ac.View;')
$sources=@(); $hashes=@()
function Add-Source([string]$relative,[string]$text) {
 $script:hashes+= [ordered]@{path=$relative;sha256=([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','')}
 foreach($match in [regex]::Matches($text,'(?m)^using [^;]+;\s*$')){$script:usings+=$match.Value.Trim()}
 $script:sources+=[regex]::Replace($text,'(?m)^using [^;]+;\s*$','')
}
foreach($f in $files){$relative="client/Assets/Scripts/$f";Add-Source $relative (Read-Source $relative)}
Add-Source 'client/Assets/Tests/CodecSuite.cs' (Read-Source 'client/Assets/Tests/CodecSuite.cs')
$boot=Read-Source 'client/Assets/Tests/BootSuite.cs'
function Method([string]$name) {
 $m=[regex]::Match($boot,'(?m)^        private static [^\r\n]+\b'+$name+'\(')
 if(!$m.Success){throw "Missing BootSuite method $name"}
 $start=$m.Index; $open=$boot.IndexOf('{',$start);$depth=1;$end=$open+1
 while($depth -gt 0 -and $end -lt $boot.Length){if($boot[$end] -eq '{'){$depth++};if($boot[$end] -eq '}'){$depth--};$end++}
 return $boot.Substring($start,$end-$start)
}
$methods=@('ChecksHudAmmoRage','MatchStateHeader','MatchStateBytes','WriteU16' | ForEach-Object {Method $_})
Add-Source 'client/Assets/Tests/BootSuite.cs selected methods verbatim' ('namespace Ac.Tests { internal static class BootBaseline {'+($methods -join "`n")+'public static void Run(){ChecksHudAmmoRage();}}}')
$stubs=@'
namespace UnityEngine {
 public static class Debug { public static void Log(object message) {System.Console.WriteLine(message);} }
 public enum KeyCode {Alpha1=49,Alpha2=50,Alpha3=51,W,S,A,D,LeftShift,RightShift,Space,Mouse0,R,E,F,Q,Return}
 public static class Application {public static bool isPlaying {get{return false;}} public static bool isFocused {get{return true;}} public static bool isBatchMode {get{return true;}} public static string dataPath;public static string persistentDataPath {get{throw new Exception("No persistence");}}}
 public static class Input {public static float GetAxisRaw(string n){throw new Exception("No Unity input");}public static bool GetKey(KeyCode k){throw new Exception("No Unity input");}}
 public class Font {public static Font CreateDynamicFontFromOSFont(string[] names,int size){throw new Exception("No Unity fonts");}}
}
namespace Ac.Net {
 public enum ConnectionState {Disconnected=0,Connecting=1,Connected=2,Zombie=3,Reconnecting=4}
 public sealed class UdpTransport {
 public event Action<ConnectionState,ConnectionState> StateChanged {add{}remove{}}
 public ushort Session {get{return 0;}}public ConnectionState State {get{return ConnectionState.Disconnected;}}public int ReconnectAttempts {get{return 0;}}
 public bool Send(PacketType type,byte[] b){throw new Exception("No network");}public bool SendJoin(string n){throw new Exception("No network");}public void Poll(int l){throw new Exception("No network");}
 }
}
namespace Ac.View {public static class CrateVisuals {public const byte KindPickup=3;}}
namespace Ac.Core {
 public enum MixBus {Master,Sfx,Music,Subtitle}
 public static class AudioSeam {public static void SetVolume(MixBus bus,float value){throw new Exception("No audio");}}
 public static class SelfTest {
 public static readonly System.Collections.Generic.List<string> EqualTrace=new System.Collections.Generic.List<string>();
 public static void Equal(long e,long a){EqualTrace.Add("expected="+e+" actual="+a);if(e!=a)throw new Exception("expected="+e+" actual="+a);}
 public static void Equal(string e,string a){if(e!=a)throw new Exception("expected="+e+" actual="+a);}
 public static void True(bool value,string e,string a){if(!value)throw new Exception(e+": "+a);}
 public static Action Codec;
 public static void Add(string n,Action test){if(n=="codec.roundtrip")Codec=test;}
}}
namespace ClientBaseline {
 public sealed class Result {public string name,error,stack;public string[] assertions;public bool passed;}
 public static class Checks {
 public static Result Run(int test){var r=new Result{name=test==0?"boot.hud_ammo_rage_from_match_state":"codec.roundtrip"};Ac.Core.SelfTest.EqualTrace.Clear();try{if(test==0)Ac.Tests.BootBaseline.Run();else{Ac.Tests.CodecSuite.Register();Ac.Core.SelfTest.Codec();}r.passed=true;}catch(Exception e){r.error=e.Message;r.stack=e.StackTrace;}r.assertions=Ac.Core.SelfTest.EqualTrace.ToArray();return r;}
 }
}
'@
$source=(($usings | Select-Object -Unique) -join "`n")+"`n"+$stubs+"`n"+($sources -join "`n")
[IO.File]::WriteAllText((Join-Path $dest 'compiled.cs'),$source)
$options=New-Object 'System.Collections.Generic.Dictionary[string,string]';$options['CompilerDirectoryPath']=$RoslynDirectory
$provider=New-Object Microsoft.CSharp.CSharpCodeProvider($options)
Add-Type -TypeDefinition $source -CodeDomProvider $provider -ErrorAction Stop
# Real RepoPaths resolves only this side's exported fixtures, never the live worktree for HEAD.
$assets=Join-Path $dest 'client/Assets';New-Item -ItemType Directory -Force $assets | Out-Null
[UnityEngine.Application]::dataPath=$assets
$fixtureFiles=@(git -C $root ls-tree -r --name-only $headId -- server/tests/fixtures docs/evidence/fixtures/trig-table.json | Where-Object {$_ -match '\.(hex|json)$'})
foreach($f in $fixtureFiles){$p=Join-Path $dest $f;New-Item -ItemType Directory -Force (Split-Path -Parent $p) | Out-Null;[IO.File]::WriteAllText($p,(Read-Source $f))}
[Ac.Sim.TrigTable]::Install([IO.File]::ReadAllText((Join-Path $dest 'docs/evidence/fixtures/trig-table.json')))
$rows=@([ClientBaseline.Checks]::Run(0),[ClientBaseline.Checks]::Run(1))
$report=[ordered]@{mode=$Mode;head=$headId;sourceHashes=$hashes;tests=$rows;limitations='Offline real production GameLoop/Net/Sim/UI and unchanged selected tests; no Unity runtime, rendering, input, fonts, persistence, or transport. Source using directives hoisted for Add-Type. Expected failures are reported, not fixed.'}
[IO.File]::WriteAllText((Join-Path $dest 'results.json'),($report | ConvertTo-Json -Depth 8))
$rows | Select-Object name,passed,error | Format-Table -AutoSize
