[CmdletBinding()]
param(
    [string]$Adb = 'adb',
    [Parameter(Mandatory = $true)]
    [string]$AdapterPath,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot 'out\camera-compatibility.properties'
}
$adapter = (Resolve-Path -LiteralPath $AdapterPath -ErrorAction Stop).Path

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

function Assert-Hash([string]$Value, [string]$Label) {
    if ($Value -notmatch '^[0-9a-f]{64}$') {
        throw "无法生成 $Label SHA-256；请确认 adb shell 已获 ROOT 且目标文件存在。返回：$Value"
    }
}

function Get-FirstField([string]$Value) {
    return (($Value -split '\s+')[0]).ToLowerInvariant()
}

$manufacturer = Invoke-AdbText @('shell', 'getprop', 'ro.product.manufacturer')
$product = Invoke-AdbText @('shell', 'getprop', 'ro.product.name')
$device = Invoke-AdbText @('shell', 'getprop', 'ro.product.device')
$api = Invoke-AdbText @('shell', 'getprop', 'ro.build.version.sdk')
$systemFingerprint = Invoke-AdbText @('shell', 'getprop', 'ro.build.fingerprint')
$vendorFingerprint = Invoke-AdbText @('shell', 'getprop', 'ro.vendor.build.fingerprint')
$cameraHash = Get-FirstField (Invoke-AdbText @(
    'shell', 'su', '-c', 'sha256sum /system/bin/cameraserver'
))
$providerCommand = 'hashes=""; for f in /vendor/bin/hw/*camera*provider* /vendor/lib64/hw/*camera*provider* /vendor/lib64/*camera*provider*; do [ -f "$f" ] && hashes="$hashes$(sha256sum "$f")
"; done; [ -n "$hashes" ] || { printf MISSING; exit; }; printf %s "$hashes" | sort | sha256sum'
$graphicsCommand = 'hashes=""; for f in /vendor/lib64/hw/*mapper* /vendor/lib64/hw/*allocator* /vendor/lib64/*mapper* /vendor/lib64/*allocator*; do [ -f "$f" ] && hashes="$hashes$(sha256sum "$f")
"; done; [ -n "$hashes" ] || { printf MISSING; exit; }; printf %s "$hashes" | sort | sha256sum'
$providerHash = Get-FirstField (Invoke-AdbText @('shell', 'su', '-c', $providerCommand))
$graphicsHash = Get-FirstField (Invoke-AdbText @('shell', 'su', '-c', $graphicsCommand))
$adapterHash = (Get-FileHash -LiteralPath $adapter -Algorithm SHA256).Hash.ToLowerInvariant()
$systemHash = Get-Sha256Text $systemFingerprint
$vendorHash = Get-Sha256Text $vendorFingerprint

if ($manufacturer -notmatch '^(Google|google)$' -or
        $device -notmatch '^(flame|coral|sunfish|bramble|redfin|barbet|oriole|raven|bluejay)$') {
    throw "不是当前 Pixel 4-6 验收矩阵：$manufacturer / $device"
}
if ($api -notmatch '^(30|31|32|33|34|35)$') {
    throw "不是 Android 11-15：API $api"
}
Assert-Hash $cameraHash 'cameraserver'
Assert-Hash $providerHash 'camera provider 集合'
Assert-Hash $graphicsHash 'graphics mapper/allocator 集合'
Assert-Hash $adapterHash 'adapter'

$canonical = @(
    $manufacturer, $product, $device, $api, $systemHash, $vendorHash,
    $cameraHash, $providerHash, $graphicsHash
) -join '|'
$compatibilityId = Get-Sha256Text $canonical
$content = @(
    "manufacturer=$manufacturer"
    "product=$product"
    "device=$device"
    "api=$api"
    "system_fingerprint_sha256=$systemHash"
    "vendor_fingerprint_sha256=$vendorHash"
    "cameraserver_sha256=$cameraHash"
    "camera_provider_sha256=$providerHash"
    "graphics_stack_sha256=$graphicsHash"
    "adapter_sha256=$adapterHash"
    "compatibility_id=$compatibilityId"
) -join "`n"
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFull
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[IO.File]::WriteAllText($outputFull, $content + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "已写入：$outputFull"
Write-Host "设备=$device API=$api compatibility_id=$compatibilityId"
