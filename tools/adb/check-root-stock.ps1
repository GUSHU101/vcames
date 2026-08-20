[CmdletBinding()]
param([string]$Serial = '')

$ErrorActionPreference = 'Stop'
$adb = (Get-Command 'adb.exe' -ErrorAction Stop).Source
$selector = if ([string]::IsNullOrWhiteSpace($Serial)) { @() } else { @('-s', $Serial) }

function Invoke-Shell([string]$Command) {
    $output = & $adb @selector shell $Command 2>&1
    return (($output -join "`n").Trim())
}

function Invoke-RootShell([string]$Command) {
    $output = & $adb @selector shell su -c $Command 2>&1
    return (($output -join "`n").Trim())
}

& $adb @selector get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw '没有可用的 ADB 设备。' }

$rootUid = Invoke-RootShell 'id -u'
if ($rootUid -ne '0') {
    throw "未获得 Root shell。请在 KernelSU/Magisk 中允许 ADB shell；返回：$rootUid"
}

$model = Invoke-Shell 'getprop ro.product.model'
$device = Invoke-Shell 'getprop ro.product.device'
$apiText = Invoke-Shell 'getprop ro.build.version.sdk'
$release = Invoke-Shell 'getprop ro.build.version.release'
$kernel = Invoke-RootShell 'uname -r'
$selinux = Invoke-RootShell 'getenforce'
$videoNode = Invoke-RootShell 'ls -lZ /dev/video100 2>/dev/null || true'
$card = Invoke-RootShell 'cat /sys/class/video4linux/video100/name 2>/dev/null || true'
$module = Invoke-RootShell 'cat /sys/module/v4l2loopback/version 2>/dev/null || grep v4l2loopback /proc/modules || true'
$provider = Invoke-RootShell '(lshal 2>/dev/null; service list 2>/dev/null) | grep "camera.provider.*external/0" || true'
$providerBinary = Invoke-RootShell 'ls /vendor/bin/hw/android.hardware.camera.provider@2.4-external-service 2>/dev/null || true'
$manifest = Invoke-RootShell 'grep -R "external/0" /vendor/etc/vintf/manifest* 2>/dev/null | head -n 3 || true'
$kernelConfig = Invoke-RootShell 'if [ -r /proc/config.gz ]; then zcat /proc/config.gz | grep -E "CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE=|CONFIG_MODVERSIONS="; fi'

$api = 0
[void][int]::TryParse($apiText, [ref]$api)
$supportedDevice = $device -in @('flame','coral','sunfish','bramble','redfin','barbet','oriole','raven','bluejay')
$hasVideo = -not [string]::IsNullOrWhiteSpace($videoNode)
$hasProvider = $provider -match 'external/0'

Write-Host "设备：$model ($device) · Android $release / API $api · kernel $kernel"
Write-Host "Root：UID $rootUid · SELinux：$selinux"
Write-Host "内核配置：$kernelConfig"
Write-Host
Write-Host ($(if ($supportedDevice) {'[PASS]'} else {'[FAIL]'}) + ' Pixel 4-6 设备代号')
Write-Host ($(if ($api -ge 30 -and $api -le 35) {'[PASS]'} else {'[FAIL]'}) + ' API 30-35')
Write-Host ($(if ($selinux -eq 'Enforcing') {'[PASS]'} else {'[FAIL]'}) + ' SELinux enforcing')
Write-Host ($(if ($hasVideo) {'[PASS]'} else {'[NEED]'}) + ' /dev/video100')
Write-Host ($(if ($card -match 'VCamES') {'[PASS]'} else {'[INFO]'}) + " card=$card module=$module")
Write-Host ($(if ($hasProvider) {'[PASS]'} else {'[NEED]'}) + ' camera provider external/0')
Write-Host "Stock Provider binary：$providerBinary"
Write-Host "Stock VINTF：$manifest"

if ($supportedDevice -and $api -ge 30 -and $api -le 35 -and
        $selinux -eq 'Enforcing' -and $hasVideo -and $hasProvider) {
    Write-Host "`nREADY：可安装不含内核/Provider payload 的 Root Bridge。"
    exit 0
}
if (-not $hasVideo) {
    Write-Host "`n需要与 $kernel 完全匹配且可通过签名/KMI校验的 v4l2loopback.ko，或包含该驱动的 custom boot kernel。"
}
if (-not $hasProvider) {
    Write-Host '需要与当前 Android vendor 匹配、且能在启动早期 VINTF 中声明 external/0 的 External Camera Provider。'
}
exit 2
