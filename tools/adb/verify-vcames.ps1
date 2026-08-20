[CmdletBinding()]
param([string]$Serial = '')

$ErrorActionPreference = 'Stop'
$adb = (Get-Command 'adb.exe' -ErrorAction Stop).Source
$selector = if ([string]::IsNullOrWhiteSpace($Serial)) { @() } else { @('-s', $Serial) }

function Invoke-AdbShell {
    param([Parameter(Mandatory = $true)][string]$Command)
    $output = & $adb @selector shell $Command 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell 失败：$Command`n$($output -join "`n")"
    }
    return (($output -join "`n").Trim())
}

& $adb @selector get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw '没有可用的 ADB 设备。' }

$model = Invoke-AdbShell 'getprop ro.product.model'
$device = Invoke-AdbShell 'getprop ro.product.device'
$apiText = Invoke-AdbShell 'getprop ro.build.version.sdk'
$release = Invoke-AdbShell 'getprop ro.build.version.release'
$kernel = Invoke-AdbShell 'uname -r'
$enforcing = Invoke-AdbShell 'getenforce'
$service = Invoke-AdbShell 'getprop init.svc.vcamesd'
$node = Invoke-AdbShell 'ls -lZ /dev/video100 2>/dev/null || true'
$card = Invoke-AdbShell 'cat /sys/class/video4linux/video100/name 2>/dev/null || true'
$process = Invoke-AdbShell 'ps -AZ | grep "[v]camesd" || true'
$provider = Invoke-AdbShell 'lshal 2>/dev/null | grep "camera.provider.*external/0" || service list | grep -i camera'
$cameraDump = Invoke-AdbShell 'dumpsys media.camera 2>/dev/null | grep -E "Camera ID|external|Device 200" | head -n 30'
$denials = Invoke-AdbShell 'logcat -b all -d 2>/dev/null | grep "avc:  denied" | grep -E "vcames|video100" | tail -n 20 || true'

$api = 0
[void][int]::TryParse($apiText, [ref]$api)
$checks = [ordered]@{
    'Pixel 4-6' = $model -match 'Pixel (4|5|6)'
    'API 30-35' = $api -ge 30 -and $api -le 35
    'SELinux enforcing' = $enforcing -eq 'Enforcing'
    'vcamesd running' = $service -eq 'running'
    '/dev/video100' = -not [string]::IsNullOrWhiteSpace($node)
    'VCamES card name' = $card -match 'VCamES'
    'vcamesd SELinux domain' = $process -match 'u:r:vcamesd:s0'
    'external provider' = $provider -match 'external/0'
    'no related AVC denial' = [string]::IsNullOrWhiteSpace($denials)
}

Write-Host "设备：$model ($device) · Android $release / API $api · kernel $kernel"
foreach ($entry in $checks.GetEnumerator()) {
    $mark = if ($entry.Value) { '[PASS]' } else { '[FAIL]' }
    Write-Host "$mark $($entry.Key)"
}
if ($cameraDump) {
    Write-Host "`nCameraService 摘要：`n$cameraDump"
}
if ($denials) {
    Write-Host "`n相关 SELinux denial：`n$denials"
}

if ($checks.Values -contains $false) { exit 1 }
Write-Host "`nVCamES 系统链路基础检查通过。请再用 Camera2 测试应用确认画面。"
