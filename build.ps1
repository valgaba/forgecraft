param([switch]$Deploy, [switch]$Test)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'svn\build.ps1') -Deploy:$Deploy -Test:$Test
