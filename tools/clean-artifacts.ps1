# Requires: Windows PowerShell 5.1+
# Safe, allow-listed cleanup of regenerable build/runtime artifacts.
# Default mode is DRY RUN. Pass -Apply to delete.
# It never follows directory reparse points (junctions/symlinks), so pnpm stores
# and external directories cannot be deleted through a link.
[CmdletBinding()]
param(
  [switch]$Apply,
  [switch]$IncludeUnityCache,
  [switch]$IncludeCodelyTooling,
  [switch]$IncludeExternalBackups,
  [switch]$SelfTest,
  [switch]$SkipStats
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$RepoPrefix = $RepoRoot.TrimEnd('\') + '\'

if (-not ('AcSafeTree' -as [type])) {
  $code = @'
using System;
using System.IO;
using System.Collections.Generic;

public static class AcSafeTree
{
    private static string Long(string p)
    {
        if (p.Length >= 240 && !p.StartsWith(@"\\?\")) return @"\\?\" + p;
        return p;
    }

    private static bool IsReparse(FileAttributes a)
    {
        return (a & FileAttributes.ReparsePoint) != 0;
    }

    // Deletes a file/directory tree; reparse points are unlinked, never followed.
    public static void Delete(string path)
    {
        string p = Long(path);
        FileAttributes attr = File.GetAttributes(p);
        if (IsReparse(attr))
        {
            if ((attr & FileAttributes.Directory) != 0) Directory.Delete(p, false);
            else File.Delete(p);
            return;
        }
        if ((attr & FileAttributes.Directory) != 0)
        {
            foreach (string child in Directory.EnumerateFileSystemEntries(p)) Delete(child);
            Directory.Delete(p, false);
        }
        else
        {
            File.SetAttributes(p, FileAttributes.Normal);
            File.Delete(p);
        }
    }

    // Returns { files, dirs, bytes }; does not follow reparse points.
    public static long[] Stats(string path)
    {
        long files = 0, dirs = 0, bytes = 0;
        var stack = new Stack<string>();
        stack.Push(path);
        while (stack.Count > 0)
        {
            string p = Long(stack.Pop());
            FileAttributes attr = File.GetAttributes(p);
            bool isDir = (attr & FileAttributes.Directory) != 0;
            if (IsReparse(attr))
            {
                if (isDir) dirs++; else files++;
                continue;
            }
            if (isDir)
            {
                dirs++;
                foreach (string child in Directory.EnumerateFileSystemEntries(p)) stack.Push(child);
            }
            else
            {
                files++;
                try { bytes += new FileInfo(p).Length; } catch { }
            }
        }
        return new long[] { files, dirs, bytes };
    }
}
'@
  Add-Type -TypeDefinition $code -Language CSharp
}

$repoTargets = @(
  'build',
  'client/Build',
  'client/Logs',
  'client/TestResults',
  'client/UserSettings',
  'server/build',
  'data',
  'ac-gate-solo-data',
  'ac-runtime-test-data',
  'ac-runtime-expedition-test-data',
  'ac-runtime-expedition-recovery-data',
  'server/ac-gate-data',
  'server/ac-gate-solo-data',
  'server/ac-runtime-test-data',
  'server-src-upload.tar',
  'tools/client-screen.png',
  'tools/failed.png'
)
if ($IncludeUnityCache) { $repoTargets += 'client/Library' }
if ($IncludeCodelyTooling) { $repoTargets += @('client/.codely-cli', 'client/.codely.packages') }

$externalTargets = @(
  'D:\projects\tmp\angry-chen-bak',
  'D:\projects\tmp\angry-chen-fixture',
  'D:\projects\tmp\angry-chen-permission-recovery',
  'D:\projects\tmp\ac-c14-review',
  'D:\projects\tmp\ac_s13_probe',
  'D:\projects\tmp\ac_size_probe.cpp',
  'D:\projects\tmp\ac_size_probe.exe',
  'D:\projects\tmp\g17_probe.cpp',
  'D:\projects\tmp\g17_probe.exe',
  'D:\projects\tmp\g17_probe2.cpp',
  'D:\projects\tmp\g17_probe2.exe',
  'D:\projects\tmp\o09-load.mjs',
  'D:\projects\tmp\o10-cov.mjs',
  'D:\projects\tmp\o10-cov.log',
  'D:\projects\tmp\o10-commit-msg.txt',
  'D:\projects\tmp\o10b-commit-msg.txt',
  'D:\projects\tmp\rev_s04.diff',
  'D:\projects\tmp\s09-commit-msg.txt',
  'D:\projects\tmp\s13_refactor.mjs'
)

$tracked = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($rel in (git -C $RepoRoot ls-files)) { [void]$tracked.Add(($rel -replace '\\', '/')) }

function Test-ContainsTracked([string]$relativeDir) {
  $prefix = ($relativeDir.TrimEnd('/')) + '/'
  foreach ($t in $tracked) { if ($t.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { return $true } }
  return $false
}

function Get-TargetStats([string]$path) {
  if (Test-Path -LiteralPath $path -PathType Leaf) {
    $f = Get-Item -LiteralPath $path -Force
    return [pscustomobject]@{ Files = 1; Dirs = 0; Bytes = $f.Length }
  }
  $s = [AcSafeTree]::Stats($path)
  return [pscustomobject]@{ Files = $s[0]; Dirs = $s[1]; Bytes = $s[2] }
}

function Format-Size([long]$bytes) {
  if ($bytes -ge 1GB) { return ('{0:N2} GB' -f ($bytes / 1GB)) }
  if ($bytes -ge 1MB) { return ('{0:N2} MB' -f ($bytes / 1MB)) }
  if ($bytes -ge 1KB) { return ('{0:N2} KB' -f ($bytes / 1KB)) }
  return ('{0} B' -f $bytes)
}

$mode = if ($Apply) { 'APPLY' } else { 'DRY RUN' }
Write-Output "=== clean-artifacts ($mode) ==="
Write-Output "repo: $RepoRoot"

if ($SelfTest) {
  $sandbox = Join-Path $env:TEMP ('ac-safe-delete-' + [guid]::NewGuid().ToString('N'))
  $target = Join-Path $env:TEMP ('ac-safe-target-' + [guid]::NewGuid().ToString('N'))
  $link = Join-Path $sandbox 'link'
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Set-Content -LiteralPath (Join-Path $target 'keep.txt') -Value 'must survive junction deletion'
  New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
  New-Item -ItemType Junction -Path $link -Target $target | Out-Null
  [AcSafeTree]::Delete($sandbox)
  if (-not (Test-Path -LiteralPath (Join-Path $target 'keep.txt'))) {
    throw 'SELFTEST FAILED: junction target was followed/deleted'
  }
  if (Test-Path -LiteralPath $sandbox) { throw 'SELFTEST FAILED: sandbox not removed' }
  Remove-Item -LiteralPath $target -Recurse -Force
  Write-Output 'SELFTEST OK: junction was unlinked, target preserved'
  exit 0
}

# Special case: root Assets/ is only removed when empty and untracked.
$assetsPath = Join-Path $RepoRoot 'Assets'
$assetsDelete = $false
if ((Test-Path -LiteralPath $assetsPath) -and -not (Test-ContainsTracked 'Assets')) {
  $assetsChildren = @(Get-ChildItem -LiteralPath $assetsPath -Force)
  if ($assetsChildren.Count -eq 0) { $assetsDelete = $true }
}

$targets = New-Object System.Collections.ArrayList
foreach ($rel in $repoTargets) {
  $full = [IO.Path]::GetFullPath((Join-Path $RepoRoot $rel))
  if (-not $full.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "refusing path outside repo: $full" }
  if (Test-ContainsTracked $rel) { throw "refusing tracked path: $rel" }
  [void]$targets.Add([pscustomobject]@{ Path = $full; Rel = $rel; External = $false })
}
if ($assetsDelete -and -not ($repoTargets -contains 'Assets')) {
  [void]$targets.Add([pscustomobject]@{ Path = (Join-Path $RepoRoot 'Assets'); Rel = 'Assets'; External = $false })
}
if ($IncludeExternalBackups) {
  foreach ($full in $externalTargets) {
    if ([IO.Path]::GetFullPath($full) -ne $full) { throw "non-canonical external path: $full" }
    [void]$targets.Add([pscustomobject]@{ Path = $full; Rel = $full; External = $true })
  }
}

$totalFiles = 0; $totalBytes = 0; $deleted = 0; $missing = 0
foreach ($t in $targets) {
  if (-not (Test-Path -LiteralPath $t.Path)) {
    Write-Output ("  [skip] {0} (not present)" -f $t.Rel)
    $missing++
    continue
  }
  if ($SkipStats) {
    $stats = [pscustomobject]@{ Files = 0; Dirs = 0; Bytes = 0 }
  } else {
    $stats = Get-TargetStats $t.Path
  }
  $totalFiles += $stats.Files
  $totalBytes += $stats.Bytes
  if ($SkipStats) {
    Write-Output ("  [del ] {0}" -f $t.Rel)
  } else {
    Write-Output ("  [del ] {0}`n           {1:N0} files / {2:N0} dirs / {3}" -f $t.Rel, $stats.Files, $stats.Dirs, (Format-Size $stats.Bytes))
  }
  if ($Apply) {
    try {
      [AcSafeTree]::Delete($t.Path)
      if (Test-Path -LiteralPath $t.Path) { throw "still exists after delete: $($t.Path)" }
      $deleted++
      Write-Output "           deleted"
    } catch {
      Write-Output ("           FAILED: {0}" -f $_.Exception.Message)
      throw
    }
  }
}

if (-not $IncludeUnityCache) { Write-Output '  [keep] client/Library (pass -IncludeUnityCache to delete)' }
if (-not $IncludeCodelyTooling) { Write-Output '  [keep] client/.codely-cli, client/.codely.packages (required by manifest; pass -IncludeCodelyTooling to delete)' }
if (-not $IncludeExternalBackups) { Write-Output '  [keep] external backups (pass -IncludeExternalBackups to delete)' }

Write-Output ''
Write-Output ("targets={0} present={1} deleted={2} files={3:N0} size={4}" -f $targets.Count, ($targets.Count - $missing), $deleted, $totalFiles, (Format-Size $totalBytes))
if (-not $Apply) {
  Write-Output 'DRY RUN complete; re-run with -Apply to delete.'
  exit 0
}
Write-Output 'CLEANUP OK'
exit 0
