[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -match '^UnityEngine' }) { throw 'Refusing Unity assemblies.' }
# Reuse numerical engine stubs, never execute the geometry script or start Unity.
$geometry = Get-Content (Join-Path $PSScriptRoot 'sheep-geometry-check.ps1') -Raw -Encoding UTF8
$stub = [regex]::Match($geometry, '(?s)\$stub = @''\r?\n(.*?)\r?\n''@').Groups[1].Value
$stub = [regex]::Replace($stub, '(?m)^ +public class SheepInstancePool .*(?:\r?\n|$)', '')
$stub = [regex]::Replace($stub, '(?m)^ +public struct SheepInstance .*(?:\r?\n|$)', '')
# EntityView is a reference type in production; the geometry DTO is otherwise sufficient here.
$stub = $stub.Replace('public struct EntityView', 'public class EntityView').Replace('public ushort Id;', 'public ushort Id; public uint Lifetime;')
$stub += 'namespace Ac.View { public static class EntityViews { public const int Capacity = 1024; } }'
$sources = foreach ($file in @('SheepMesh.cs','SheepInstancePool.cs','SheepVisuals.cs')) {
 [regex]::Replace((Get-Content (Join-Path $root ('client/Assets/Scripts/View/' + $file)) -Raw -Encoding UTF8), '(?m)^using [^;]+;\s*$', '')
}
$suite = Get-Content (Join-Path $root 'client/Assets/Tests/SheepSuite.cs') -Raw -Encoding UTF8
$methods = [regex]::Matches($suite, '(?s)        private static void (ChecksEntityState\w+)\(\)\s*\{.*?(?=\r?\n        private static |\r?\n        //|\z)')
$checks = ($methods | ForEach-Object { $_.Value.Replace('private static void', 'public static void') }) -join "`n"
$asserts = @'
public static class SelfTest {
 public static void True(bool ok,string expected,string actual){if(!ok)throw new System.Exception(expected+": "+actual);}
 public static void Equal(long expected,long actual){if(expected!=actual)throw new System.Exception("expected="+expected+" actual="+actual);}
}
'@
$asserts += @'
public static class AllocMeter {
 public static long Begin(){return System.GC.GetAllocatedBytesForCurrentThread();}
 public static void AssertZero(long before){long delta=System.GC.GetAllocatedBytesForCurrentThread()-before;if(delta!=0)throw new System.Exception("allocated bytes="+delta);}
}
'@
$source = "using UnityEngine;`nusing Ac.View;`n" + $stub + ($sources -join "`n") + $asserts + "public static class SheepStateChecks {`n" + $checks + "`n}"
if ($PSVersionTable.PSVersion.Major -lt 7) {
 $options = New-Object 'System.Collections.Generic.Dictionary[string,string]'
 $options['CompilerDirectoryPath'] = 'D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn'
 $provider = New-Object Microsoft.CSharp.CSharpCodeProvider($options)
 Add-Type -TypeDefinition $source -CodeDomProvider $provider
} else { Add-Type -TypeDefinition $source }
$failures = 0
foreach ($method in $methods) {
 $name = $method.Groups[1].Value
 try { [SheepStateChecks].GetMethod($name).Invoke($null, @()); Write-Output "PASS $name" }
 catch { $failures++; Write-Output "FAIL $name $($_.Exception.GetBaseException().Message)" }
}
if ($methods.Count -eq 0) { throw 'No state checks found.' }
if ($failures) { exit 1 }
