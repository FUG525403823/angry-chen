[CmdletBinding()]
param(
  [string]$SourceDirectory = '',
  [string]$OutFile = '',
  [string]$RoslynDirectory = 'D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn',
  [switch]$ReportOnly,
  [switch]$NoFile
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'build/arena-model'
if (!$SourceDirectory) { $SourceDirectory = Join-Path $root 'client/Assets/Scripts/View' }
if (!$OutFile) { $OutFile = Join-Path $work 'meshes.json' }
if ([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -match '^UnityEngine' }) { throw 'Run in a fresh non-Unity PowerShell process.' }
$paths = @((Join-Path $SourceDirectory 'ArenaMesh.cs'), (Join-Path (Split-Path -Parent $SourceDirectory) 'Sim/LocalStep.cs'), (Join-Path $SourceDirectory 'Materials.cs'))
$hashes = @($paths | ForEach-Object { (Get-FileHash $_ -Algorithm SHA256).Hash })
$text = @($paths | ForEach-Object { [IO.File]::ReadAllText($_) })
$after = @($paths | ForEach-Object { (Get-FileHash $_ -Algorithm SHA256).Hash })
if (($hashes -join ',') -ne ($after -join ',')) { throw 'Production source changed while reading; rerun after the writer finishes.' }
# Balanced extraction preserves the real config body without importing unrelated simulation dependencies.
$match = [regex]::Match($text[1], 'public\s+struct\s+MoveConfig\s*\{')
if (!$match.Success) { throw 'Cannot locate real MoveConfig struct.' }
$start = $match.Index; $end = $start + $match.Length; $depth = 1
while ($depth -gt 0 -and $end -lt $text[1].Length) {
  if ($text[1][$end] -eq '{') { $depth++ }
  if ($text[1][$end] -eq '}') { $depth-- }
  $end++
}
if ($depth -ne 0) { throw 'Unbalanced MoveConfig struct.' }
$config = 'namespace Ac.Sim { ' + $text[1].Substring($start, $end - $start) + ' }'
$material = [regex]::Match($text[2], 'public\s+const\s+int\s+MaterialCount\s*=\s*\d+\s*;')
if (!$material.Success) { throw 'Cannot extract real Materials.MaterialCount literal.' }
$stub = @'
using System;
using System.Collections.Generic;
using UnityEngine;
using Ac.View;
using Ac.Sim;
namespace UnityEngine {
 public struct Vector2 {
  public float x,y; public Vector2(float x,float y){this.x=x;this.y=y;}
 }
 public struct Vector3 {
  public float x,y,z; public Vector3(float x,float y,float z){this.x=x;this.y=y;this.z=z;}
  public static Vector3 zero {get{return new Vector3();}}
  public float sqrMagnitude {get{return x*x+y*y+z*z;}}
  public float magnitude {get{return (float)Math.Sqrt(sqrMagnitude);}}
  public Vector3 normalized {get{float m=magnitude;return m>1e-20f?this/m:zero;}}
  public static Vector3 operator +(Vector3 a,Vector3 b){return new Vector3(a.x+b.x,a.y+b.y,a.z+b.z);}
  public static Vector3 operator -(Vector3 a,Vector3 b){return new Vector3(a.x-b.x,a.y-b.y,a.z-b.z);}
  public static Vector3 operator *(Vector3 a,float s){return new Vector3(a.x*s,a.y*s,a.z*s);}
  public static Vector3 operator /(Vector3 a,float s){return a*(1f/s);}
  public static float Dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  public static Vector3 Cross(Vector3 a,Vector3 b){return new Vector3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
 }
 public struct Bounds {public Vector3 min,max;public Vector3 center{get{return (min+max)*.5f;}}public Vector3 size{get{return max-min;}}}
 public class Mesh {
  public string name; public Vector3[] vertices=new Vector3[0],normals=new Vector3[0];public Vector2[] uv=new Vector2[0];public int[] triangles=new int[0];public Bounds bounds;
  public int vertexCount{get{return vertices.Length;}}
  public void SetTriangles(int[] data,int submesh){triangles=data;}
  public void RecalculateNormals(){normals=new Vector3[vertices.Length];for(int i=0;i+2<triangles.Length;i+=3){int a=triangles[i],b=triangles[i+1],c=triangles[i+2];if(a<0||b<0||c<0||a>=vertices.Length||b>=vertices.Length||c>=vertices.Length)continue;var n=Vector3.Cross(vertices[b]-vertices[a],vertices[c]-vertices[a]);normals[a]+=n;normals[b]+=n;normals[c]+=n;}for(int i=0;i<normals.Length;i++)normals[i]=normals[i].normalized;}
  public void RecalculateBounds(){if(vertices.Length==0)return;Vector3 lo=vertices[0],hi=lo;foreach(var p in vertices){lo.x=Math.Min(lo.x,p.x);lo.y=Math.Min(lo.y,p.y);lo.z=Math.Min(lo.z,p.z);hi.x=Math.Max(hi.x,p.x);hi.y=Math.Max(hi.y,p.y);hi.z=Math.Max(hi.z,p.z);}bounds=new Bounds{min=lo,max=hi};}
 }
}
'@
$checks = @'
namespace ArenaOffline {
 public sealed class MeshResult {
  public string name;public int vertices,triangles,invalidIndices,incompleteIndices,nonFiniteVertices,nonFiniteNormals,nonFiniteUvs,nonUnitNormals,degenerateTriangles,nonUpTriangles,inwardTriangles,boxNormalPlaneMismatches,degenerateUvTriangles,woodScaleTriangles,worldUvMismatches,invalidBoxLayout;
  public bool passed;public string[] failures;
 }
 public sealed class Report {public MeshResult[] meshes;public int vertices,triangles,materials,reportedDrawCalls,meshDrawCalls;public bool statsMatch,budgetPassed,passed;}
 public static class Checks {
  static bool Finite(float f){return !float.IsNaN(f)&&!float.IsInfinity(f);}
  static bool Finite(Vector3 p){return Finite(p.x)&&Finite(p.y)&&Finite(p.z);}
  static bool Finite(Vector2 p){return Finite(p.x)&&Finite(p.y);}
  static bool Scale(Vector3 a,Vector3 b,Vector2 u,Vector2 v){double dx=u.x-v.x,dy=u.y-v.y;double expected=(a-b).magnitude/2.4;return Math.Abs(Math.Sqrt(dx*dx+dy*dy)-expected)<=1e-4*Math.Max(1,expected);}
  public static MeshResult Check(Mesh m,int boxes,bool up,bool wood,double tile){
   var v=m.vertices;var t=m.triangles;var n=m.normals;var uv=m.uv;
   var r=new MeshResult{name=m.name,vertices=v.Length,triangles=t.Length/3,incompleteIndices=t.Length%3};
   int stride=boxes==0?0:v.Length/boxes;
   if(boxes>0&&(v.Length%boxes!=0||(stride!=8&&stride!=24)||t.Length!=boxes*36))r.invalidBoxLayout++;
   var centers=new Vector3[boxes];
   if(r.invalidBoxLayout==0)for(int box=0;box<boxes;box++){var lo=v[box*stride];var hi=lo;for(int i=box*stride;i<(box+1)*stride;i++){lo.x=Math.Min(lo.x,v[i].x);lo.y=Math.Min(lo.y,v[i].y);lo.z=Math.Min(lo.z,v[i].z);hi.x=Math.Max(hi.x,v[i].x);hi.y=Math.Max(hi.y,v[i].y);hi.z=Math.Max(hi.z,v[i].z);}centers[box]=(lo+hi)*.5f;}
   for(int i=0;i<v.Length;i++){
    if(!Finite(v[i]))r.nonFiniteVertices++;
    if(i>=n.Length||!Finite(n[i]))r.nonFiniteNormals++;else if(Math.Abs(n[i].magnitude-1)>1e-4)r.nonUnitNormals++;
    if(i>=uv.Length||!Finite(uv[i]))r.nonFiniteUvs++;
    if(tile>0&&uv.Length==v.Length&&i>0&&(Math.Abs((uv[i].x-uv[0].x)-(v[i].x-v[0].x)/tile)>1e-4||Math.Abs((uv[i].y-uv[0].y)-(v[i].z-v[0].z)/tile)>1e-4))r.worldUvMismatches++;
   }
   for(int i=0;i<t.Length;i++)if(t[i]<0||t[i]>=v.Length)r.invalidIndices++;
   for(int i=0;i+2<t.Length;i+=3){
    int a=t[i],b=t[i+1],c=t[i+2];if(a<0||b<0||c<0||a>=v.Length||b>=v.Length||c>=v.Length)continue;
    var cross=Vector3.Cross(v[b]-v[a],v[c]-v[a]);var face=cross.normalized;
    double edges=Math.Max((v[b]-v[a]).sqrMagnitude,Math.Max((v[c]-v[a]).sqrMagnitude,(v[c]-v[b]).sqrMagnitude));
    if(!Finite(cross)||cross.magnitude<=Math.Max(1e-12,edges*1e-6))r.degenerateTriangles++;
    if(up&&cross.y<=1e-8)r.nonUpTriangles++;
    if(boxes>0&&r.invalidBoxLayout==0){int box=a/stride;if(b/stride!=box||c/stride!=box){r.invalidBoxLayout++;}else{
     var mid=(v[a]+v[b]+v[c])/3f;float direction=Vector3.Dot(face,mid-centers[box]);if(direction<=1e-6)r.inwardTriangles++;
     var outward=direction<0?face*(-1):face;
     foreach(int k in new[]{a,b,c})if(k>=n.Length||!Finite(n[k])||(n[k]-outward).magnitude>1e-4)r.boxNormalPlaneMismatches++;
    }}
    if(a<uv.Length&&b<uv.Length&&c<uv.Length){double area=Math.Abs((double)(uv[b].x-uv[a].x)*(uv[c].y-uv[a].y)-(double)(uv[b].y-uv[a].y)*(uv[c].x-uv[a].x));if(double.IsNaN(area)||double.IsInfinity(area)||area<=1e-10)r.degenerateUvTriangles++;if(wood&&(!Scale(v[a],v[b],uv[a],uv[b])||!Scale(v[b],v[c],uv[b],uv[c])||!Scale(v[c],v[a],uv[c],uv[a])))r.woodScaleTriangles++;}
   }
   var f=new List<string>();if(v.Length==0||t.Length==0)f.Add("empty");if(r.invalidIndices+r.incompleteIndices>0)f.Add("indices");if(r.nonFiniteVertices+r.nonFiniteNormals+r.nonFiniteUvs>0)f.Add("finite-or-missing-attributes");if(r.nonUnitNormals>0)f.Add("unit-normals");if(r.degenerateTriangles>0)f.Add("degenerate");if(r.nonUpTriangles>0)f.Add("upward-winding");if(r.inwardTriangles>0)f.Add("box-outward-winding");if(r.invalidBoxLayout>0)f.Add("box-layout");if(r.boxNormalPlaneMismatches>0)f.Add("box-hard-plane-normals");if(r.degenerateUvTriangles>0)f.Add("uv-area");if(r.woodScaleTriangles>0)f.Add("wood-world-scale");if(r.worldUvMismatches>0)f.Add("world-xz-uv");r.failures=f.ToArray();r.passed=f.Count==0;return r;
  }
  public static Report Run(){
   var p=ArenaParams.Default();var arena=new ArenaMesh(p);var stats=arena.Build();
   var rows=new[]{Check(arena.GroundMesh,0,true,false,6),Check(arena.DirtYardMesh,0,true,false,4),Check(arena.OuterRingMesh,0,true,false,6),Check(arena.FenceMesh,4,false,true,0),Check(arena.BarnMesh,1,false,false,0),Check(arena.BarnRoofMesh,1,false,false,0),Check(arena.HayBaleMesh,1,false,false,0)};
   var r=new Report{meshes=rows,materials=stats.Materials,reportedDrawCalls=stats.DrawCalls,meshDrawCalls=rows.Length,passed=true};foreach(var row in rows){r.vertices+=row.vertices;r.triangles+=row.triangles;r.passed&=row.passed;}
   r.statsMatch=r.vertices==stats.Vertices&&r.triangles==stats.Triangles;r.budgetPassed=r.vertices<=4096&&r.triangles<=3072&&r.materials<=6&&r.reportedDrawCalls<=8&&r.meshDrawCalls<=8;r.passed&=r.statsMatch&&r.budgetPassed;return r;
  }
 }
}
'@
$source = $stub + "`n" + $config + "`nnamespace Ac.View { public static class Materials { " + $material.Value + " } }`n" + $text[0].Replace('using Ac.Sim;', '').Replace('using UnityEngine;', '') + "`n" + $checks
if ($PSVersionTable.PSVersion.Major -lt 7) {
  if (!(Test-Path (Join-Path $RoslynDirectory 'csc.exe'))) { throw 'Existing Roslyn csc.exe required; nothing is installed.' }
  $options = New-Object 'System.Collections.Generic.Dictionary[string,string]'
  $options['CompilerDirectoryPath'] = $RoslynDirectory
  $provider = New-Object Microsoft.CSharp.CSharpCodeProvider($options)
  Add-Type -TypeDefinition $source -CodeDomProvider $provider -ErrorAction Stop
} else { Add-Type -TypeDefinition $source -ErrorAction Stop }
try { $result = [ArenaOffline.Checks]::Run() }
catch { $result = [pscustomobject]@{ passed=$false; failures=@('production-build-exception'); error=$_.Exception.GetBaseException().Message } }
$report = [ordered]@{
  mode='offline-numeric-stubs-not-unity'; passed=$result.passed
  source=[ordered]@{ arena=$paths[0]; arenaSha256=$hashes[0]; localStep=$paths[1]; localStepSha256=$hashes[1]; materials=$paths[2]; materialsSha256=$hashes[2] }
  limits=[ordered]@{ vertices=4096; triangles=3072; materials=6; drawCalls=8 }
  tolerances=[ordered]@{ degenerateDoubleArea='max(1e-12, maxEdgeSquared*1e-6)'; normal=1e-4; uvDoubleArea=1e-10; uvScale=1e-4; worldUv=1e-4 }
  limitations=@('Real ArenaMesh source is compiled unchanged except hoisted using directives. Only the complete real MoveConfig struct is extracted from LocalStep; movement simulation is not executed.', 'Materials stub contains only the real MaterialCount literal; no materials, shaders, textures, engine lifetime or GPU rendering are tested.', 'Mesh normals use area-weighted numerical recalculation; not exact Unity engine behavior. Boxes must have independent 8- or 24-vertex contiguous blocks (four fence boxes, one per other box mesh).', 'Wood scale checks apply to the textured fence only: all triangle edge UV lengths must equal world lengths / 2.4. Untextured barn, roof and hay still require nonzero UV triangle area.', 'World XZ UV checks allow a constant translation, at 6m grass/ring and 4m dirt. Roof box silhouette is a design choice, not a failure.', 'Reported draw calls come from Build; meshDrawCalls is seven separate meshes as a conservative unbatched diagnostic, not a GPU measurement.')
  result=$result
}
[ordered]@{ passed=$report.passed; out=$OutFile; result=$result } | ConvertTo-Json -Depth 7 -Compress
if (!$NoFile) {
  $directory = Split-Path -Parent ([IO.Path]::GetFullPath($OutFile))
  if (!(Test-Path $directory)) { New-Item -ItemType Directory -Path $directory -Force | Out-Null }
  [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), ($report | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
}
if (!$report.passed -and !$ReportOnly) { exit 1 }
