[CmdletBinding()]
param(
  [string]$SourceDirectory = '',
  [string]$OutFile = '',
  [string]$BootSuitePath = '',
  [string]$RoslynDirectory = 'D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn',
  [switch]$ReportOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$SourceDirectory) { $SourceDirectory = Join-Path $root 'client/Assets/Scripts' }
if (!$OutFile) { $OutFile = Join-Path $root 'build/movement-stutter/results.json' }
if ([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -match '^UnityEngine' }) { throw 'Refusing to load or run with Unity assemblies.' }
$files = @('Boot/GameLoop.cs','View/EntityViews.cs','View/Interpolation.cs','View/ErrorSmoother.cs','Core/InputSampler.cs','Core/MiniJson.cs','Core/FrameProfiler.cs')
$files += @(Get-ChildItem (Join-Path $SourceDirectory 'Sim') -Filter '*.cs' | Where-Object { $_.Name -notin @('FixtureLoader.cs','RepoPaths.cs') } | ForEach-Object { 'Sim/' + $_.Name })
$files += @(Get-ChildItem (Join-Path $SourceDirectory 'Net') -Filter '*.cs' | Where-Object { $_.Name -in @('CommandCodec.cs','EventCodec.cs','EventIdTracker.cs','LocalIdentity.cs','MatchStateCodec.cs','PacketReader.cs','PacketWriter.cs','SnapshotCodec.cs') } | ForEach-Object { 'Net/' + $_.Name })
$sources = @(); $hashes = @(); $usings = @('using System;','using Ac.Boot;','using Ac.Core;','using Ac.Net;','using Ac.Sim;','using Ac.UI;','using Ac.View;')
foreach ($rel in $files) {
  $path = Join-Path $SourceDirectory $rel
  $before = (Get-FileHash $path -Algorithm SHA256).Hash
  $text = [IO.File]::ReadAllText($path)
  if ($before -ne (Get-FileHash $path -Algorithm SHA256).Hash) { throw "Source changed while reading: $rel" }
  $hashes += [ordered]@{ path=$rel; sha256=$before }
  $directives = [regex]::Matches($text,'(?m)^using [^;]+;\s*$')
  foreach ($directive in $directives) { $usings += $directive.Value.Trim() }
  $sources += [regex]::Replace($text,'(?m)^using [^;]+;\s*$','')
}
$stubs = @'
namespace UnityEngine {
 public enum KeyCode { Alpha1 = 49, Alpha2 = 50, Alpha3 = 51, W,S,A,D,LeftShift,RightShift,Space,Mouse0,R,E,F,Q,Return }
 public static class Application { public static bool isPlaying { get { return false; } } public static bool isFocused { get { return true; } } }
 public static class Input { public static float GetAxisRaw(string name){throw new System.InvalidOperationException("Use injected input only");} public static bool GetKey(KeyCode code){throw new System.InvalidOperationException("Use injected input only");} }
}
namespace Ac.Sim { public static class RepoPaths { public static string Locate(string path) { throw new System.InvalidOperationException("Install real trig JSON explicitly"); } } }
namespace Ac.Net {
 public enum ConnectionState { Disconnected=0,Connecting=1,Connected=2,Zombie=3,Reconnecting=4 }
 public sealed class UdpTransport {
  public event System.Action<ConnectionState,ConnectionState> StateChanged { add {} remove {} }
  public ushort Session { get { return 0; } } public ConnectionState State { get { return ConnectionState.Disconnected; } }
  public int ReconnectAttempts { get { return 0; } }
  public bool Send(PacketType type,byte[] bytes){throw new System.InvalidOperationException("No network in harness");}
  public bool SendJoin(string name){throw new System.InvalidOperationException("No network in harness");}
  public void Poll(int limit){throw new System.InvalidOperationException("No network in harness");}
 }
}
namespace Ac.UI {
 public struct HudEvent {public EventType Type;public int HitFlags,TargetId,SubjectId,Wave,ReviveRatio255,WaveSize;public short HitX,HitY,HitZ;}
 public struct HudSample {public int HpRatio255,Mag,MagSize,Reserve,ReloadLeft10Ms,Rage,RageLeft100Ms,Wave,IntermissionMs;public byte Phase;public bool Downed,RageMode,Reloading,Charging;public float SpreadDeg;}
 public sealed class Hud {public const byte PhaseLobby=0;public static bool CombatUiVisible(byte phase){return phase==2||phase==3;}public void Apply(HudSample s){}public void Tick(float dt){}public void PushEvent(HudEvent e){}}
}
'@
$checks = @'
namespace MovementOffline {
 public sealed class Result {
  public double stopDrift, fractionalError;public int droppedSteps;
  public string name;public int fps,frames,snapshots,localSteps,commands,freezes,backwards,checkedFrames;
  public double minStep=double.MaxValue,maxStep,meanStep,distance,expectedDistance;public bool passed;public string error;
 }
 public static class Checks {
  static SnapshotFrame Snapshot(uint tick,uint time,ushort id,short x,short z) {
   return new SnapshotFrame {Tick=tick,ServerTimeMs=time,Entities=new[]{new FrameEntity{Id=id,KindFlags=0,XCm=x,ZCm=z,HpRatioUnits=255}},EntityCount=1,RemovedIds=new ushort[0]};
  }
  static GameLoop Loop(){return new GameLoop(new SnapshotView(),new EntityViews(),new Hud(),new FrameProfiler());}
  static void Observe(Result r,double step){r.checkedFrames++;r.minStep=Math.Min(r.minStep,step);r.maxStep=Math.Max(r.maxStep,step);r.meanStep+=step;if(Math.Abs(step)<1e-8)r.freezes++;if(step < -1e-8)r.backwards++;}
  public static Result Remote(int fps){
   var r=new Result{name="remote-20Hz-snapshots",fps=fps};var loop=Loop();double dt=1000.0/fps;double previous=0;uint tick=0;int nextSnapshot=0;
   for(int frame=0;frame<fps*3;frame++){
    double now=frame*dt;
    while(nextSnapshot<=now+1e-8){tick++;if(!loop.ApplySnapshot(Snapshot(tick,(uint)nextSnapshot,17,(short)(nextSnapshot*0.3),0)))throw new Exception("snapshot rejected");nextSnapshot+=50;}
    loop.Frame(dt);EntityView view;if(!loop.Views.TryGet(17,out view))throw new Exception("remote view missing");
    if(frame>=fps)Observe(r,view.RenderX-previous);previous=view.RenderX;
   }
   r.frames=loop.Frames;r.snapshots=loop.SnapshotsApplied;r.distance=previous;r.meanStep/=r.checkedFrames;
   r.passed=r.freezes==0&&r.backwards==0&&r.maxStep<=0.075+1e-7&&Math.Abs(r.meanStep-3.0/fps)<0.002;
   return r;
  }
  public static Result Local(int fps){
   var r=new Result{name="local-real-30Hz-input",fps=fps};var loop=Loop();loop.LocalPlayerId=1;
   if(!loop.ApplySnapshot(Snapshot(1,0,1,1000,0)))throw new Exception("local seed rejected");loop.Frame(0);
   var sampler=new InputSampler();sampler.SetKey(UnityEngine.KeyCode.W,true);loop.Sampler=sampler;
   double dt=1000.0/fps,previous=0;
   for(int frame=0;frame<fps*3;frame++){
    loop.Frame(dt);EntityView view;if(!loop.Views.TryGet(1,out view))throw new Exception("local view missing");
    if(frame>=fps)Observe(r,view.RenderZ-previous);previous=view.RenderZ;
   }
   r.frames=loop.Frames;r.snapshots=loop.SnapshotsApplied;r.localSteps=loop.LocalSteps;r.commands=sampler.Seq;r.distance=previous;r.expectedDistance=13.5;r.meanStep/=r.checkedFrames;
   sampler.SetKey(UnityEngine.KeyCode.W,false);
   for(int i=0;i<fps/2;i++)loop.Frame(dt);
   EntityView stopped;loop.Views.TryGet(1,out stopped);double stop=stopped.RenderZ;
   for(int i=0;i<fps/2;i++)loop.Frame(dt);
   loop.Views.TryGet(1,out stopped);r.stopDrift=Math.Abs(stopped.RenderZ-stop);
   r.passed=r.freezes==0&&r.backwards==0&&r.localSteps>=59&&r.localSteps<=60&&Math.Abs(r.distance-r.expectedDistance)<=0.20&&r.maxStep<=4.5/fps+1e-6&&r.stopDrift<1e-8;
   return r;
  }
  public static Result Fractional(int fps){
   var r=new Result{name="predictor-fractional-time",fps=fps};var predictor=new Predictor();predictor.SetAuthoritative(10,0,0,0,0);
   var command=new StepCommand{MoveX=127};double dt=1000.0/fps;
   var advance=typeof(Predictor).GetMethod("Advance");var parameterType=advance.GetParameters()[0].ParameterType;
   for(int i=0;i<fps*3;i++){
    // Baseline exposes int dt; reflect the real signature rather than editing old source to accept doubles.
    object elapsed;if(parameterType==typeof(int))elapsed=(int)dt;else elapsed=dt;
    advance.Invoke(predictor,new object[]{elapsed,command});
   }
   double x,y,z;predictor.RenderPosition(out x,out y,out z);r.localSteps=predictor.Steps;r.distance=z;r.expectedDistance=13.5;r.fractionalError=Math.Abs(z-13.5);r.droppedSteps=predictor.DroppedSubsteps;
   r.passed=r.localSteps==60&&r.fractionalError<1e-7&&r.droppedSteps==0;return r;
  }
 }
}
'@
# Extract only the existing pure RunWalk regressions verbatim; unrelated network/GUI BootSuite cases are not compiled.
$bootPath=$BootSuitePath
if(!$bootPath){$bootPath=Join-Path $root 'client/Assets/Tests/BootSuite.cs'}
$boot=[IO.File]::ReadAllText($bootPath)
$walkStart=$boot.IndexOf('        private static void ChecksLocalMotionIndependentOfSendRate()')
$walkEnd=$boot.IndexOf('        private static void ChecksHudAmmoRage()')
$newLoopStart=$boot.IndexOf('        private static GameLoop NewLoop(')
$newLoopEnd=$boot.IndexOf('        private static void Feed(', $newLoopStart)
if($walkStart -lt 0 -or $walkEnd -le $walkStart -or $newLoopStart -lt 0 -or $newLoopEnd -le $newLoopStart){throw 'Existing BootSuite extraction anchors changed.'}
$existing='namespace MovementOffline { public static class ExistingWalk {'+$boot.Substring($walkStart,$walkEnd-$walkStart)+$boot.Substring($newLoopStart,$newLoopEnd-$newLoopStart)+@'
 public static Result Run(int test) {var names=new[]{"existing-boot.local_pose_from_prediction","existing-boot.no_camera_flicker","boot.remote_motion_render_cadence","boot.local_motion_independent_of_send_rate"};var r=new Result{name=names[test],fps=test<2?40:0};try{if(test==0)ChecksLocalPoseFromPrediction();else if(test==1)ChecksNoCameraFlicker();else if(test==2)ChecksRemoteMotionRenderCadence();else ChecksLocalMotionIndependentOfSendRate();r.passed=true;}catch(Exception e){r.error=e.Message;}return r;}
 }}
 namespace Ac.Core { public static class SelfTest {
 public static void Equal(long expected,long actual){if(expected!=actual)throw new Exception("expected="+expected+" actual="+actual);}
 public static void True(bool value,string expected,string actual){if(!value)throw new Exception(expected+": "+actual);}
 }}
'@
$hashes += [ordered]@{path='Tests/BootSuite.cs extracted movement methods';actualPath=(Resolve-Path $bootPath).Path;sha256=(Get-FileHash $bootPath -Algorithm SHA256).Hash}
$source = (($usings | Select-Object -Unique) -join "`n") + "`n" + $stubs + "`n" + ($sources -join "`n") + "`n" + $checks + "`n" + $existing
if ($PSVersionTable.PSVersion.Major -lt 7) {
 if (!(Test-Path (Join-Path $RoslynDirectory 'csc.exe'))) {throw 'Installed Roslyn is required; no installation will be attempted.'}
 $options = New-Object 'System.Collections.Generic.Dictionary[string,string]';$options['CompilerDirectoryPath']=$RoslynDirectory
 $provider = New-Object Microsoft.CSharp.CSharpCodeProvider($options)
 Add-Type -TypeDefinition $source -CodeDomProvider $provider -ErrorAction Stop
} else { Add-Type -TypeDefinition $source -ErrorAction Stop }
[Ac.Sim.TrigTable]::Install([IO.File]::ReadAllText((Join-Path $root 'docs/evidence/fixtures/trig-table.json')))
$rows=@()
foreach($fps in @(60,120,144,240)) {
 foreach($test in @('Remote','Local','Fractional')) {
  try { if($test -eq 'Remote'){$rows += [MovementOffline.Checks]::Remote($fps)}elseif($test -eq 'Local'){$rows += [MovementOffline.Checks]::Local($fps)}else{$rows += [MovementOffline.Checks]::Fractional($fps)} }
  catch { $rows += [pscustomobject]@{name=$test;fps=$fps;passed=$false;error=$_.Exception.GetBaseException().Message} }
 }
}
foreach($case in @(0,1,2,3)){$rows += [MovementOffline.ExistingWalk]::Run($case)}
$report=[ordered]@{
 mode='actual-pure-production-with-external-stubs-not-unity';passed=@($rows|Where-Object {!$_.passed}).Count -eq 0
 sourceDirectory=$SourceDirectory;sourceHashes=$hashes
 limitations=@('Actual GameLoop Frame/ApplySnapshot, EntityViews, SnapshotView, Interpolation, Predictor, LocalStep, Reconciler, CommandBuffer and InputSampler are compiled from source unchanged; not a client build.', 'Only injected InputSampler.SetKey is exercised. Unity Input stubs throw if reached; Unity DLLs are neither loaded nor referenced.', 'HUD is no-op and transport is disconnected/throwing; no GUI, audio, game, socket, GPU or Unity runtime validation.', 'Remote constant velocity trace is synthetic, in-order, no loss/jitter; local motion uses one authoritative seed outside barn then real 30Hz input with no subsequent reconciliation.')
 tests=$rows
}
$dir=Split-Path -Parent $OutFile;if(!(Test-Path $dir)){New-Item -ItemType Directory -Force $dir|Out-Null}
[IO.File]::WriteAllText($OutFile,($report|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
[ordered]@{passed=$report.passed;out=$OutFile;tests=@($rows|Select-Object name,fps,passed,localSteps,freezes,backwards,minStep,maxStep,distance,stopDrift,error)}|ConvertTo-Json -Depth 6 -Compress
if(!$report.passed -and !$ReportOnly){exit 1}
