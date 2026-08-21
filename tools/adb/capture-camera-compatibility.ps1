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

function Get-VendorFamily([string]$Manufacturer, [string]$Brand) {
    $identity = "$Manufacturer|$Brand".ToLowerInvariant()
    if ($identity -match 'google') { return 'google' }
    if ($identity -match 'xiaomi|redmi|poco') { return 'xiaomi' }
    if ($identity -match 'samsung') { return 'samsung' }
    return 'unsupported'
}

function Get-SocFamily([string]$Identity) {
    $value = $Identity.ToLowerInvariant()
    if ($value -match 'tensor|gs101|gs201') { return 'tensor' }
    if ($value -match 'qualcomm|snapdragon|qcom|msm|\bsm\d{3,4}') { return 'qualcomm' }
    if ($value -match 'exynos') { return 'exynos' }
    if ($value -match 'mediatek|mtk|\bmt\d{4}') { return 'mediatek' }
    return 'unknown'
}

$manufacturer = Invoke-AdbText @('shell', 'getprop', 'ro.product.manufacturer')
$brand = Invoke-AdbText @('shell', 'getprop', 'ro.product.brand')
$product = Invoke-AdbText @('shell', 'getprop', 'ro.product.name')
$device = Invoke-AdbText @('shell', 'getprop', 'ro.product.device')
$api = Invoke-AdbText @('shell', 'getprop', 'ro.build.version.sdk')
$socManufacturer = Invoke-AdbText @('shell', 'getprop', 'ro.soc.manufacturer')
$socModel = Invoke-AdbText @('shell', 'getprop', 'ro.soc.model')
$boardPlatform = Invoke-AdbText @('shell', 'getprop', 'ro.board.platform')
$hardware = Invoke-AdbText @('shell', 'getprop', 'ro.hardware')
$vendorFamily = Get-VendorFamily $manufacturer $brand
$socFamily = Get-SocFamily "$socManufacturer|$socModel|$boardPlatform|$hardware"
$systemFingerprint = Invoke-AdbText @('shell', 'getprop', 'ro.build.fingerprint')
$vendorFingerprint = Invoke-AdbText @('shell', 'getprop', 'ro.vendor.build.fingerprint')
$cameraServices = Invoke-AdbText @(
    'shell', 'su', '-c',
    '(lshal 2>/dev/null; service list 2>/dev/null; grep -R -E "camera.provider|ICameraProvider" /vendor/etc/vintf/manifest* /vendor/etc/vintf/manifest/*.xml /system/etc/vintf/manifest* 2>/dev/null) | head -n 120'
)
$hasAidl = $cameraServices -match 'android\.hardware\.camera\.provider\.ICameraProvider|format="aidl"|ICameraProvider/'
$hasHidl = $cameraServices -match 'camera\.provider@[0-9]|format="hidl"'
$cameraHalTransport = if ($hasAidl -and $hasHidl) {
    'mixed'
} elseif ($hasAidl) {
    'aidl'
} elseif ($hasHidl) {
    'hidl'
} else {
    'unknown'
}

$cameraHash = Get-FirstField (Invoke-AdbText @(
    'shell', 'su', '-c', 'sha256sum /system/bin/cameraserver'
))
$providerCommand = 'hashes=""; for f in /vendor/bin/hw/*camera*provider* /vendor/lib64/hw/*camera*provider* /vendor/lib64/*camera*provider*; do [ -f "$f" ] && hashes="$hashes$(sha256sum "$f")
"; done; [ -n "$hashes" ] || { printf MISSING; exit; }; printf %s "$hashes" | sort | sha256sum'
$vendorCameraCommand = 'hashes=""; for f in /vendor/bin/hw/*camera* /vendor/lib64/hw/*camera* /vendor/lib64/*camera*.so /vendor/lib64/*camera*/*; do [ -f "$f" ] && hashes="$hashes$(sha256sum "$f")
"; done; [ -n "$hashes" ] || { printf MISSING; exit; }; printf %s "$hashes" | sort | sha256sum'
$graphicsCommand = 'hashes=""; for f in /vendor/lib64/hw/*mapper* /vendor/lib64/hw/*allocator* /vendor/lib64/*mapper* /vendor/lib64/*allocator*; do [ -f "$f" ] && hashes="$hashes$(sha256sum "$f")
"; done; [ -n "$hashes" ] || { printf MISSING; exit; }; printf %s "$hashes" | sort | sha256sum'
$providerHash = Get-FirstField (Invoke-AdbText @('shell', 'su', '-c', $providerCommand))
$vendorCameraHash = Get-FirstField (Invoke-AdbText @('shell', 'su', '-c', $vendorCameraCommand))
$graphicsHash = Get-FirstField (Invoke-AdbText @('shell', 'su', '-c', $graphicsCommand))
$adapterHash = (Get-FileHash -LiteralPath $adapter -Algorithm SHA256).Hash.ToLowerInvariant()
$systemHash = Get-Sha256Text $systemFingerprint
$vendorHash = Get-Sha256Text $vendorFingerprint

if ($vendorFamily -eq 'unsupported') {
    throw "仅支持 Google、小米/Redmi/POCO、Samsung：$manufacturer / $brand"
}
if ($socFamily -eq 'unknown') {
    throw "无法识别 SoC family：$socManufacturer / $socModel / $boardPlatform / $hardware"
}
if ($cameraHalTransport -eq 'unknown') {
    throw '无法从 service/VINTF 识别 Camera HIDL/AIDL transport。'
}
if ($api -notmatch '^(30|31|32|33)$') {
    throw "仅支持 Android 11-13：API $api"
}
Assert-Hash $cameraHash 'cameraserver'
Assert-Hash $providerHash 'camera provider 集合'
Assert-Hash $vendorCameraHash 'vendor camera 库集合'
Assert-Hash $graphicsHash 'graphics mapper/allocator 集合'
Assert-Hash $adapterHash 'adapter'

$canonical = @(
    $vendorFamily, $socFamily, $cameraHalTransport,
    $manufacturer, $product, $device, $api, $systemHash, $vendorHash,
    $cameraHash, $providerHash, $vendorCameraHash, $graphicsHash
) -join '|'
$compatibilityId = Get-Sha256Text $canonical
$content = @(
    'schema=2'
    "vendor_family=$vendorFamily"
    "soc_family=$socFamily"
    "camera_hal_transport=$cameraHalTransport"
    "manufacturer=$manufacturer"
    "brand=$brand"
    "product=$product"
    "device=$device"
    "api=$api"
    "system_fingerprint_sha256=$systemHash"
    "vendor_fingerprint_sha256=$vendorHash"
    "cameraserver_sha256=$cameraHash"
    "camera_provider_sha256=$providerHash"
    "vendor_camera_libraries_sha256=$vendorCameraHash"
    "graphics_stack_sha256=$graphicsHash"
    "adapter_sha256=$adapterHash"
    "compatibility_id=$compatibilityId"
) -join "`n"
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFull
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[IO.File]::WriteAllText($outputFull, $content + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "已写入：$outputFull"
Write-Host "vendor=$vendorFamily soc=$socFamily camera=$cameraHalTransport device=$device API=$api"
Write-Host "compatibility_id=$compatibilityId"
