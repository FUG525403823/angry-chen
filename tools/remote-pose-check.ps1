[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Compile production pose code and the same checks registered by Unity; no sockets or Unity process.
$files = @(
    'client/Assets/Scripts/Sim/Quantize.cs'
    'client/Assets/Scripts/Sim/TrigTable.cs'
    'client/Assets/Scripts/Core/MiniJson.cs'
    'client/Assets/Scripts/Sim/SnapshotView.cs'
    'client/Assets/Scripts/View/Interpolation.cs'
    'client/Assets/Scripts/View/EntityViews.cs'
    'client/Assets/Scripts/View/ErrorSmoother.cs'
    'client/Assets/Tests/RemotePoseChecks.cs'
)
$usings = @()
$sources = foreach ($file in $files) {
    $text = [IO.File]::ReadAllText((Join-Path $root $file))
    foreach ($match in [regex]::Matches($text, '(?m)^using [^;]+;\s*$')) {
        $usings += $match.Value.Trim()
    }
    [regex]::Replace($text, '(?m)^using [^;]+;\s*$', '')
}
$stubs = @'
namespace Ac.Sim { public static class RepoPaths { public static string Locate(string path){ throw new InvalidOperationException("No file lookup in pose checks"); } } }
namespace Ac.Net { public struct EntityRecord { public const byte KindMask=3; } }
namespace Ac.Core {
    public static class SelfTest {
        public static readonly System.Collections.Generic.Dictionary<string, Action> Checks =
            new System.Collections.Generic.Dictionary<string, Action>();
        public static void Add(string name, Action test) { Checks.Add(name, test); }
    }
}
'@
$source = (($usings | Select-Object -Unique) -join "`n") + "`n" + ($sources -join "`n") + "`n" + $stubs
if ($PSVersionTable.PSVersion.Major -lt 7) {
    $options = New-Object 'System.Collections.Generic.Dictionary[string,string]'
    $options['CompilerDirectoryPath'] = 'D:\tools\Visualstudio\software\MSBuild\Current\Bin\Roslyn'
    $provider = New-Object Microsoft.CSharp.CSharpCodeProvider($options)
    Add-Type -TypeDefinition $source -CodeDomProvider $provider
} else {
    Add-Type -TypeDefinition $source
}

[Ac.Tests.RemotePoseChecks]::Register()
$failures = 0
foreach ($check in [Ac.Core.SelfTest]::Checks.GetEnumerator()) {
    try {
        $check.Value.Invoke()
        Write-Output "PASS $($check.Key)"
    } catch {
        $failures++
        Write-Output "FAIL $($check.Key) $($_.Exception.GetBaseException().Message)"
    }
}
Write-Output "Remote pose checks: $([Ac.Core.SelfTest]::Checks.Count) total, $failures failed (in-memory, not real network)"
if ($failures) { exit 1 }
