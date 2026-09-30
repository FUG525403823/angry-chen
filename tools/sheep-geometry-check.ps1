[CmdletBinding()]
param(
  [string]$SourceDirectory = '',
  [string]$OutFile = '',
  [string]$GeometryOutFile = '',
  [string]$RoslynDirectory = 'D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn',
  [switch]$ReportOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'build/sheep-model'
if (!$SourceDirectory) { $SourceDirectory = Join-Path $root 'client/Assets/Scripts/View' }
if (!$OutFile) { $OutFile = Join-Path $work 'meshes.json' }
$meshPath = Join-Path $SourceDirectory 'SheepMesh.cs'
$visualsPath = Join-Path $SourceDirectory 'SheepVisuals.cs'
if ([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -match '^UnityEngine' }) { throw 'Refusing to run with Unity assemblies loaded.' }
$hashBefore = @((Get-FileHash $meshPath -Algorithm SHA256).Hash, (Get-FileHash $visualsPath -Algorithm SHA256).Hash)
$meshSource = [IO.File]::ReadAllText($meshPath)
$visualsSource = [IO.File]::ReadAllText($visualsPath)
$hashAfter = @((Get-FileHash $meshPath -Algorithm SHA256).Hash, (Get-FileHash $visualsPath -Algorithm SHA256).Hash)
if (($hashBefore -join ',') -ne ($hashAfter -join ',')) { throw 'Production source changed while reading; wait for writer ready.' }
$stub = @'
using System;
using System.Collections.Generic;
namespace UnityEngine {
 public struct Vector3 {
  public float x,y,z; public Vector3(float a,float b,float c){x=a;y=b;z=c;}
  public static Vector3 zero{get{return new Vector3();}} public static Vector3 one{get{return new Vector3(1,1,1);}}
  public static Vector3 up{get{return new Vector3(0,1,0);}} public static Vector3 right{get{return new Vector3(1,0,0);}} public static Vector3 forward{get{return new Vector3(0,0,1);}}
  public float sqrMagnitude{get{return x*x+y*y+z*z;}} public float magnitude{get{return (float)Math.Sqrt(sqrMagnitude);}}
  public Vector3 normalized{get{float m=magnitude;return m>1e-20f?this/m:zero;}}
  public void Normalize(){this=normalized;}
  public static Vector3 operator +(Vector3 a,Vector3 b){return new Vector3(a.x+b.x,a.y+b.y,a.z+b.z);} public static Vector3 operator -(Vector3 a,Vector3 b){return new Vector3(a.x-b.x,a.y-b.y,a.z-b.z);} public static Vector3 operator -(Vector3 a){return zero-a;}
  public static Vector3 operator *(Vector3 a,float b){return new Vector3(a.x*b,a.y*b,a.z*b);} public static Vector3 operator *(float b,Vector3 a){return a*b;} public static Vector3 operator /(Vector3 a,float b){return a*(1/b);}
  public static float Dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;} public static Vector3 Cross(Vector3 a,Vector3 b){return new Vector3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
  public static Vector3 Scale(Vector3 a,Vector3 b){return new Vector3(a.x*b.x,a.y*b.y,a.z*b.z);} public static Vector3 Lerp(Vector3 a,Vector3 b,float t){return a+(b-a)*t;}
 }
 public struct Color32 { public byte r,g,b,a;public Color32(byte r,byte g,byte b,byte a){this.r=r;this.g=g;this.b=b;this.a=a;} }
 public static class Mathf {
  public const float PI=(float)Math.PI,Rad2Deg=180f/PI,Deg2Rad=PI/180f;
  public static float Sin(float a){return (float)Math.Sin(a);} public static float Cos(float a){return (float)Math.Cos(a);} public static float Sqrt(float a){return (float)Math.Sqrt(a);} public static float Abs(float a){return Math.Abs(a);} public static float Min(float a,float b){return Math.Min(a,b);} public static float Max(float a,float b){return Math.Max(a,b);} public static float Clamp01(float a){return Math.Max(0,Math.Min(1,a));} public static float Lerp(float a,float b,float t){return a+(b-a)*Clamp01(t);} public static float Atan2(float y,float x){return (float)Math.Atan2(y,x);}
 }
 public struct Bounds { public Vector3 min,max; public Vector3 size{get{return max-min;}} public Vector3 center{get{return (min+max)*.5f;}} }
 public class Mesh {
  public string name; public Vector3[] vertices=new Vector3[0],normals=new Vector3[0];public int[] triangles=new int[0];public Color32[] colors32=new Color32[0];public Bounds bounds;public int vertexCount{get{return vertices.Length;}}
  public void SetTriangles(int[] a,int submesh){triangles=a;} public void RecalculateNormals(){normals=new Vector3[vertices.Length];for(int i=0;i<triangles.Length;i+=3){int a=triangles[i],b=triangles[i+1],c=triangles[i+2];Vector3 n=Vector3.Cross(vertices[b]-vertices[a],vertices[c]-vertices[a]);normals[a]+=n;normals[b]+=n;normals[c]+=n;}for(int i=0;i<normals.Length;i++)normals[i]=normals[i].normalized;}
  public void RecalculateBounds(){if(vertices.Length==0)return;Vector3 lo=vertices[0],hi=lo;foreach(var v in vertices){lo.x=Math.Min(lo.x,v.x);lo.y=Math.Min(lo.y,v.y);lo.z=Math.Min(lo.z,v.z);hi.x=Math.Max(hi.x,v.x);hi.y=Math.Max(hi.y,v.y);hi.z=Math.Max(hi.z,v.z);}bounds=new Bounds{min=lo,max=hi};}
 }
 public struct Quaternion {
  public float x,y,z,w; public Quaternion(float x,float y,float z,float w){this.x=x;this.y=y;this.z=z;this.w=w;}
  public static Quaternion identity{get{return new Quaternion(0,0,0,1);}}
  public static Quaternion AngleAxis(float degrees,Vector3 axis){axis=axis.normalized;double r=degrees*Math.PI/360;float s=(float)Math.Sin(r);return new Quaternion(axis.x*s,axis.y*s,axis.z*s,(float)Math.Cos(r));}
  public static Quaternion operator *(Quaternion a,Quaternion b){return new Quaternion(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);}
  public static Quaternion Euler(float x,float y,float z){return AngleAxis(y,Vector3.up)*AngleAxis(x,Vector3.right)*AngleAxis(z,Vector3.forward);}
  public static Vector3 operator *(Quaternion q,Vector3 v){Vector3 u=new Vector3(q.x,q.y,q.z);return v+2f*Vector3.Cross(u,Vector3.Cross(u,v)+q.w*v);}
 }
 public struct Matrix4x4 {
  public float m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33;
  public static Matrix4x4 TRS(Vector3 p,Quaternion q,Vector3 s){var a=q*new Vector3(s.x,0,0);var b=q*new Vector3(0,s.y,0);var c=q*new Vector3(0,0,s.z);return new Matrix4x4{m00=a.x,m10=a.y,m20=a.z,m01=b.x,m11=b.y,m21=b.z,m02=c.x,m12=c.y,m22=c.z,m03=p.x,m13=p.y,m23=p.z,m33=1};}
  public Vector3 MultiplyPoint3x4(Vector3 p){return new Vector3(m00*p.x+m01*p.y+m02*p.z+m03,m10*p.x+m11*p.y+m12*p.z+m13,m20*p.x+m21*p.y+m22*p.z+m23);}public Vector3 MultiplyVector(Vector3 p){return new Vector3(m00*p.x+m01*p.y+m02*p.z,m10*p.x+m11*p.y+m12*p.z,m20*p.x+m21*p.y+m22*p.z);}
 }
}
namespace Ac.View {
 public static class ArenaMesh {public static uint Hash(uint state){unchecked{state+=0x6D2B79F5u;var t=state;t=(t^(t>>15))*(t|1u);t^=t+(t^(t>>7))*(t|61u);return t^(t>>14);}}}
 public static class ArtPalette {public static UnityEngine.Color32 Hex(int c){return new UnityEngine.Color32((byte)(c>>16),(byte)(c>>8),(byte)c,255);}}
 public struct EntityView {public bool Visible; public byte Kind,State,Flags;public ushort Id;public double X,Y,Z,YawRad,HpRatio;}
 public struct SheepInstance {public UnityEngine.Matrix4x4 Transform,EmblemTransform;public float Darken,HpRatio,EmblemIntensity;public byte Kind,State;public bool Visible;}
 public class SheepInstancePool {public const int EntityCapacity=1024;public SheepInstance[] Instances=new SheepInstance[EntityCapacity];int at;public void Reset(){at=0;}public int TryAcquire(int kind,out int index){index=at++;return index<EntityCapacity?index:-1;}}
}

'@
$checks = @'
using System;
using System.Collections.Generic;
using UnityEngine;
using Ac.View;
namespace SheepOffline {
 public sealed class MeshResult {
  public string kind; public int vertices,triangles,invalidIndices,nonFiniteVertices,nonFiniteNormals,degenerateTriangles,nonUnitNormals,normalFaceDisagreements,radialInwardTriangles,indexComponents,aabbIslands,unmirroredVertices,emblemTriangles,emblemNonForwardTriangles;
  public double minY,height,expectedLocalHeight,maxMirrorError,minDoubleArea,visualScale,expectedVisualScale,visualHeight,expectedVisualHeight,anchorError,emblemNormalError,corpseAnchorError,corpseNormalError,corpseScale,expectedCorpseScale;
  public bool vertexBudget,triangleBudget,colorsMatch,grounded,heightNormalized,symmetric,anchorAvailable,visualScaleOnce,visualAnchorMatches,passed;
  public string[] failures;
 }
 public static class Checks {
  static bool Finite(float x){return !float.IsNaN(x)&&!float.IsInfinity(x);}static bool Finite(Vector3 p){return Finite(p.x)&&Finite(p.y)&&Finite(p.z);}
  static int Root(int[] p,int x){while(p[x]!=x){p[x]=p[p[x]];x=p[x];}return x;}static void Union(int[] p,int a,int b){p[Root(p,a)]=Root(p,b);}
  public static MeshResult Run(SheepKind kind){
   var mesh=SheepMesh.Build(kind);var v=mesh.vertices;var t=mesh.triangles;var normals=mesh.normals;var form=SheepMesh.Form(kind);
   var r=new MeshResult{kind=kind.ToString(),vertices=v.Length,triangles=t.Length/3,vertexBudget=v.Length<=512,triangleBudget=t.Length/3<=512,colorsMatch=mesh.colors32.Length==v.Length,minDoubleArea=double.MaxValue,anchorError=-1};
   var parents=new int[v.Length];for(int i=0;i<v.Length;i++)parents[i]=i;
   for(int i=0;i<v.Length;i++){if(!Finite(v[i]))r.nonFiniteVertices++;if(i>=normals.Length||!Finite(normals[i]))r.nonFiniteNormals++;else if(Math.Abs(normals[i].magnitude-1)>1e-4)r.nonUnitNormals++;}
   for(int i=0;i<t.Length;i++)if(t[i]<0||t[i]>=v.Length)r.invalidIndices++;
   if(r.invalidIndices==0)for(int i=0;i<t.Length;i+=3){Union(parents,t[i],t[i+1]);Union(parents,t[i],t[i+2]);}
   var centers=new Dictionary<int,Vector3>();var counts=new Dictionary<int,int>();var lows=new Dictionary<int,Vector3>();var highs=new Dictionary<int,Vector3>();
   for(int i=0;i<v.Length;i++){int k=Root(parents,i);if(!centers.ContainsKey(k)){centers[k]=Vector3.zero;counts[k]=0;lows[k]=v[i];highs[k]=v[i];}centers[k]+=v[i];counts[k]++;Vector3 lo=lows[k],hi=highs[k];lo.x=Math.Min(lo.x,v[i].x);lo.y=Math.Min(lo.y,v[i].y);lo.z=Math.Min(lo.z,v[i].z);hi.x=Math.Max(hi.x,v[i].x);hi.y=Math.Max(hi.y,v[i].y);hi.z=Math.Max(hi.z,v[i].z);lows[k]=lo;highs[k]=hi;}
   var keys=new List<int>(centers.Keys);foreach(int k in keys)centers[k]/=counts[k];r.indexComponents=keys.Count;
   var boxes=new int[keys.Count];for(int i=0;i<boxes.Length;i++)boxes[i]=i;
   for(int i=0;i<keys.Count;i++)for(int j=i+1;j<keys.Count;j++){var a=lows[keys[i]];var b=highs[keys[i]];var c=lows[keys[j]];var d=highs[keys[j]];if(a.x<=d.x+1e-5&&c.x<=b.x+1e-5&&a.y<=d.y+1e-5&&c.y<=b.y+1e-5&&a.z<=d.z+1e-5&&c.z<=b.z+1e-5)Union(boxes,i,j);}
   var islands=new HashSet<int>();for(int i=0;i<boxes.Length;i++)islands.Add(Root(boxes,i));r.aabbIslands=islands.Count;
   if(r.invalidIndices==0)for(int i=0;i<t.Length;i+=3){int a=t[i],b=t[i+1],c=t[i+2];Vector3 cross=Vector3.Cross(v[b]-v[a],v[c]-v[a]);double area=cross.magnitude;r.minDoubleArea=Math.Min(r.minDoubleArea,area);double edgeSquared=Math.Max((v[b]-v[a]).sqrMagnitude,Math.Max((v[c]-v[a]).sqrMagnitude,(v[c]-v[b]).sqrMagnitude));if(area<=Math.Max(1e-12,edgeSquared*1e-6)){r.degenerateTriangles++;continue;}Vector3 face=cross.normalized;var mid=(v[a]+v[b]+v[c])/3;
    if(Vector3.Dot(face,mid-centers[Root(parents,a)]) < -1e-6)r.radialInwardTriangles++;
    if(a<normals.Length&&b<normals.Length&&c<normals.Length&&Vector3.Dot(face,normals[a]+normals[b]+normals[c])<=1e-5)r.normalFaceDisagreements++;
   }
   for(int i=0;i<v.Length;i++){Vector3 reflected=new Vector3(-v[i].x,v[i].y,v[i].z);double best=double.MaxValue;for(int j=0;j<v.Length;j++)best=Math.Min(best,(v[j]-reflected).magnitude);r.maxMirrorError=Math.Max(r.maxMirrorError,best);if(best>1e-5)r.unmirroredVertices++;}
   r.minY=mesh.bounds.min.y;r.height=mesh.bounds.size.y;r.expectedLocalHeight=form.HeightM/form.Scale;r.grounded=Math.Abs(r.minY)<1e-5;r.heightNormalized=Math.Abs(r.height-r.expectedLocalHeight)<1e-4;r.symmetric=r.unmirroredVertices==0;
   var pool=new SheepInstancePool();var visuals=new SheepVisuals(pool);visuals.BeginFrame();var view=new EntityView{Visible=true,Kind=(byte)kind,Id=17,HpRatio=1,X=2,Y=3,Z=4,YawRad=Math.PI/2};int index=visuals.Write(view,0);if(index<0)throw new InvalidOperationException("Write rejected visible sheep");var instance=pool.Instances[index];r.visualScale=instance.Transform.MultiplyVector(Vector3.up).magnitude;r.expectedVisualScale=form.Scale*SheepMesh.WoolJitter(kind,view.Id);r.visualScaleOnce=Math.Abs(r.visualScale-r.expectedVisualScale)<1e-5;r.visualHeight=r.height*r.visualScale;r.expectedVisualHeight=form.HeightM*SheepMesh.WoolJitter(kind,view.Id);
   var anchor=typeof(SheepMesh).GetMethod("EmblemAnchor",new[]{typeof(SheepKind)});r.anchorAvailable=anchor!=null;if(anchor!=null){Vector3 local=(Vector3)anchor.Invoke(null,new object[]{kind});r.anchorError=(instance.Transform.MultiplyPoint3x4(local)-instance.EmblemTransform.MultiplyPoint3x4(Vector3.zero)).magnitude;r.visualAnchorMatches=r.anchorError<1e-5;}
   var emblem=SheepMesh.BuildEmblem(kind);r.emblemTriangles=emblem.triangles.Length/3;for(int i=0;i<emblem.triangles.Length;i+=3){var a=emblem.vertices[emblem.triangles[i]];var b=emblem.vertices[emblem.triangles[i+1]];var c=emblem.vertices[emblem.triangles[i+2]];var face=Vector3.Cross(b-a,c-a);if(!Finite(face)||face.z<=1e-12)r.emblemNonForwardTriangles++;}
   // Expected normal comes from the agreed -55 degree forehead tilt, not the production transform.
   var expectedNormal=Quaternion.Euler(0,(float)view.YawRad*Mathf.Rad2Deg,0)*(Quaternion.Euler(-55,0,0)*Vector3.forward);r.emblemNormalError=(instance.EmblemTransform.MultiplyVector(Vector3.forward).normalized-expectedNormal).magnitude;
   view.State=SheepVisuals.StateDead;visuals.BeginFrame();int firstCorpse=visuals.Write(view,100);if(firstCorpse<0)throw new InvalidOperationException("Initial corpse rejected");visuals.BeginFrame();int halfCorpse=visuals.Write(view,850);if(halfCorpse<0)throw new InvalidOperationException("Half-faded corpse rejected");var corpse=pool.Instances[halfCorpse];r.corpseScale=corpse.Transform.MultiplyVector(Vector3.up).magnitude;r.expectedCorpseScale=r.expectedVisualScale*(1.0-(1.0-SheepVisuals.CorpseShrink)*0.5);r.corpseNormalError=(corpse.EmblemTransform.MultiplyVector(Vector3.forward).normalized-expectedNormal).magnitude;r.corpseAnchorError=-1;if(anchor!=null){Vector3 local=(Vector3)anchor.Invoke(null,new object[]{kind});r.corpseAnchorError=(corpse.Transform.MultiplyPoint3x4(local)-corpse.EmblemTransform.MultiplyPoint3x4(Vector3.zero)).magnitude;}
   var fail=new List<string>();if(r.emblemTriangles==0||r.emblemNonForwardTriangles!=0)fail.Add("emblem-forward-faces");if(r.emblemNormalError>1e-5)fail.Add("emblem-normal-tilt");if(r.corpseAnchorError<0||r.corpseAnchorError>1e-5)fail.Add("corpse-anchor");if(r.corpseNormalError>1e-5)fail.Add("corpse-normal-tilt");if(Math.Abs(r.corpseScale-r.expectedCorpseScale)>1e-5)fail.Add("corpse-half-shrink");if(!r.vertexBudget||!r.triangleBudget)fail.Add("budget");if(r.invalidIndices>0)fail.Add("indices");if(r.nonFiniteVertices+r.nonFiniteNormals>0)fail.Add("finite");if(r.degenerateTriangles>0)fail.Add("degenerate-triangles");if(r.nonUnitNormals>0)fail.Add("normal-unit");if(r.normalFaceDisagreements>0)fail.Add("normal-face-agreement");if(!r.colorsMatch)fail.Add("colors");if(!r.grounded)fail.Add("ground");if(!r.heightNormalized)fail.Add("normalized-height");if(!r.symmetric)fail.Add("bilateral-symmetry");if(!r.visualScaleOnce)fail.Add("visual-scale");if(!r.anchorAvailable||!r.visualAnchorMatches)fail.Add("emblem-anchor");r.failures=fail.ToArray();r.passed=fail.Count==0;return r;
  }
 }
}

'@
# Hoist using directives only so separate real production files can share one Add-Type compilation unit.
# Production classes/method bodies are not rewritten; no Unity DLL is referenced.
$source = $stub + "`n" + $meshSource.Replace('using UnityEngine;', '') + "`n" + $visualsSource.Replace('using UnityEngine;', '') + "`n" + $checks.Replace('using System;', '').Replace('using System.Collections.Generic;', '').Replace('using UnityEngine;', '').Replace('using Ac.View;', '')
$source = "using UnityEngine;`nusing Ac.View;`n" + $source
if ($PSVersionTable.PSVersion.Major -lt 7) {
  if (!(Test-Path (Join-Path $RoslynDirectory 'csc.exe'))) { throw 'Existing Roslyn csc.exe is required for modern source syntax; nothing will be installed.' }
  $providerOptions = New-Object 'System.Collections.Generic.Dictionary[string,string]'
  $providerOptions['CompilerDirectoryPath'] = $RoslynDirectory
  $provider = New-Object Microsoft.CSharp.CSharpCodeProvider($providerOptions)
  Add-Type -TypeDefinition $source -CodeDomProvider $provider -ErrorAction Stop
} else { Add-Type -TypeDefinition $source -ErrorAction Stop }
$rows = @(0..3 | ForEach-Object {
  $kind = [Ac.View.SheepKind]$_
  try { [SheepOffline.Checks]::Run($kind) }
  catch { [pscustomobject]@{ kind = $kind.ToString(); passed = $false; failures = @('production-build-exception'); error = $_.Exception.GetBaseException().Message } }
})
if ($GeometryOutFile) {
  $geometry = @(0..3 | ForEach-Object {
    $kind = [Ac.View.SheepKind]$_
    try {
      $mesh = [Ac.View.SheepMesh]::Build($kind)
      [ordered]@{ kind=$kind.ToString(); vertices=@($mesh.vertices | ForEach-Object { ,@($_.x,$_.y,$_.z) }); triangles=$mesh.triangles; colors=@($mesh.colors32 | ForEach-Object { ,@($_.r,$_.g,$_.b,$_.a) }); normals=@($mesh.normals | ForEach-Object { ,@($_.x,$_.y,$_.z) }) }
    } catch { [ordered]@{ kind=$kind.ToString(); error=$_.Exception.GetBaseException().Message } }
  })
  $geometryDirectory = Split-Path -Parent $GeometryOutFile
  if (!(Test-Path $geometryDirectory)) { New-Item -ItemType Directory -Path $geometryDirectory -Force | Out-Null }
  [IO.File]::WriteAllText($GeometryOutFile, (@{ mode='offline-numeric-stubs-not-unity'; meshSha256=$hashBefore[0]; meshes=$geometry } | ConvertTo-Json -Depth 8 -Compress), (New-Object Text.UTF8Encoding($false)))
}
$report = [ordered]@{
  mode = 'offline-numeric-stubs-not-unity'
  source = [ordered]@{ mesh = $meshPath; meshSha256 = $hashBefore[0]; visuals = $visualsPath; visualsSha256 = $hashBefore[1] }
  passed = @($rows | Where-Object { !$_.passed }).Count -eq 0
  tolerances = [ordered]@{ degenerateDoubleArea = 'max(1e-12, maxEdgeSquared * 1e-6)'; unitNormal = 1e-4; ground = 1e-5; normalizedHeight = 1e-4; bilateralMirror = 1e-5; visualScaleAndAnchor = 1e-5 }
  hashStub = 'ArenaMesh.Hash integer formula copied verbatim with unchecked uint arithmetic; not random replacement.'
  limitations = @('Not Unity compilation, engine execution, rendering or lighting validation.', 'Stub normals are area-weighted numerical approximations, not a promise of exact Unity seam handling.', 'aabbIslands is a conservative broad-phase connectivity diagnostic; overlap does not prove solid contact.', 'radialInwardTriangles is a heuristic per index-connected component, not a global outward-proof for concave tubes.', 'Stub instance pool is only a deterministic data container; no GPU, culling, materials or engine lifetime behavior.')
  meshes = $rows
}
$directory = Split-Path -Parent $OutFile
if (!(Test-Path $directory)) { New-Item -ItemType Directory -Path $directory -Force | Out-Null }
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($OutFile, $json, (New-Object Text.UTF8Encoding($false)))
$summary = [ordered]@{ mode=$report.mode; passed=$report.passed; out=$OutFile; meshes=@($rows | Select-Object kind,vertices,triangles,degenerateTriangles,nonUnitNormals,normalFaceDisagreements,radialInwardTriangles,minY,height,expectedLocalHeight,aabbIslands,maxMirrorError,anchorError,emblemTriangles,emblemNonForwardTriangles,emblemNormalError,corpseAnchorError,corpseNormalError,corpseScale,expectedCorpseScale,failures,error) }
$summary | ConvertTo-Json -Depth 6 -Compress
if (!$report.passed -and !$ReportOnly) { exit 1 }
