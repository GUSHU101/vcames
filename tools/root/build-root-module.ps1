[CmdletBinding()]
param(
    [ValidateRange(30, 33)][int]$Api = 30,
    [string]$NdkPath = '',
    [string]$ReplacementAdapter = '',
    [string]$Profile = '',
    [string]$ProfileSignature = '',
    [string]$ProfilePublicKey = '',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$builder = Join-Path $PSScriptRoot 'build_device_pack.py'
$arguments = @($builder, '--api', $Api)
if ($NdkPath) { $arguments += @('--ndk', $NdkPath) }
if ($ReplacementAdapter) { $arguments += @('--adapter', $ReplacementAdapter) }
if ($Profile) { $arguments += @('--profile', $Profile) }
if ($ProfileSignature) { $arguments += @('--profile-signature', $ProfileSignature) }
if ($ProfilePublicKey) { $arguments += @('--profile-public-key', $ProfilePublicKey) }
if ($OutputPath) { $arguments += @('--output', $OutputPath) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "VCamES build failed with exit code $LASTEXITCODE" }
