[CmdletBinding()]
param(
    [string]$Adb = 'adb',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot 'out\camera-compatibility.properties'
}

function Invoke-AdbText([string[]]$Arguments) {
    $result = & $Adb @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb 失败：$($result -join [Environment]::NewLine)"
    }
    return (($result -join "`n").Trim())
}

function Get-Sha256Text([string]$Value) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

$device = Invoke-AdbText @('shell', 'getprop', 'ro.product.device')
$api = Invoke-AdbText @('shell', 'getprop', 'ro.build.version.sdk')
$fingerprint = Invoke-AdbText @('shell', 'getprop', 'ro.build.fingerprint')
$cameraHashLine = Invoke-AdbText @(
    'shell', 'su', '-c', 'sha256sum /system/bin/cameraserver'
)
$cameraHash = ($cameraHashLine -split '\s+')[0].ToLowerInvariant()

if ($device -notmatch '^(flame|coral|sunfish|bramble|redfin|barbet|oriole|raven|bluejay)$') {
    throw "不是受支持的 Pixel 4-6 设备：$device"
}
if ($api -notmatch '^(33|34|35)$') {
    throw "不是 Android 13-15：API $api"
}
if ($cameraHash -notmatch '^[0-9a-f]{64}$') {
    throw '无法读取 cameraserver SHA-256；请确认 adb shell 已获 ROOT。'
}

$content = @(
    "device=$device"
    "api=$api"
    "fingerprint_sha256=$(Get-Sha256Text $fingerprint)"
    "cameraserver_sha256=$cameraHash"
) -join "`n"
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFull
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[IO.File]::WriteAllText($outputFull, $content + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "已写入：$outputFull"
Write-Host "设备=$device API=$api cameraserver=$cameraHash"
