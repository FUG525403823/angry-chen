[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Project,[Parameter(Mandatory=$true)][string]$Method,[Parameter(Mandatory=$true)][string]$Log,[string]$Output,[switch]$Development,[switch]$Graphics)
$ErrorActionPreference='Stop'
$unity='D:/tools/unity/unity_install/2022.3.62t16/Editor/Tuanjie.exe'
$projectPath=[IO.Path]::GetFullPath($Project)
$logPath=[IO.Path]::GetFullPath($Log)
New-Item -ItemType Directory -Force -Path (Split-Path $logPath) | Out-Null
$args=@('-batchmode','-projectPath',"`"$projectPath`"",'-executeMethod',$Method,'-logFile',"`"$logPath`"")
if($Method -ne 'Ac.Tests.PlayModeSmoke.Run'){$args+='-quit'}
if(-not $Graphics){$args+='-nographics'}
if($Output){$args+=@('-target','Windows64','-output',"`"$([IO.Path]::GetFullPath($Output))`"",'-backend','mono','-development',$(if($Development){'1'}else{'0'}))}
$p=Start-Process -FilePath $unity -ArgumentList $args -PassThru
$null=$p.Handle
$p.WaitForExit()
$code=$p.ExitCode
Write-Output "ENGINE_EXIT=$code method=$Method log=$logPath"
if($null -eq $code){exit 1}
exit $code
