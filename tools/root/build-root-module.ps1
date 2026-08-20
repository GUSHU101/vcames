[CmdletBinding()]
param(
    [ValidateRange(33, 35)]
    [int]$Api = 35,
    [string]$NdkPath = '',
    [string]$DaemonBinary = '',
    [string]$ControllerApk = '',
    [string]$KernelModule = '',
    [string]$ProviderBinary = '',
    [string]$ReplacementAdapter = '',
    [string]$CompatibilityManifest = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$outRoot = Join-Path $repoRoot 'out\root'
$generatedAssets = Join-Path $repoRoot 'app\build\generated\rootBridgeAssets'
New-Item -ItemType Directory -Force -Path $outRoot, $generatedAssets | Out-Null

function Resolve-ExistingFile([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label 不是文件：$Path"
    }
    return $resolved.Path
}

if ([string]::IsNullOrWhiteSpace($DaemonBinary)) {
    if ([string]::IsNullOrWhiteSpace($NdkPath)) { $NdkPath = $env:ANDROID_NDK_HOME }
    if ([string]::IsNullOrWhiteSpace($NdkPath)) { $NdkPath = $env:ANDROID_NDK_ROOT }
    if ([string]::IsNullOrWhiteSpace($NdkPath) -and $env:ANDROID_HOME) {
        $ndkRoot = Join-Path $env:ANDROID_HOME 'ndk'
        if (Test-Path -LiteralPath $ndkRoot) {
            $latest = Get-ChildItem -LiteralPath $ndkRoot -Directory |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($latest) { $NdkPath = $latest.FullName }
        }
    }
    if ([string]::IsNullOrWhiteSpace($NdkPath)) {
        throw '未找到 Android NDK；请设置 ANDROID_NDK_HOME 或传入 -NdkPath。'
    }
    $toolchain = Join-Path $NdkPath 'build\cmake\android.toolchain.cmake'
    if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
        throw "NDK CMake toolchain 不存在：$toolchain"
    }
    $cmakeCommand = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) { $cmakeCommand = Get-Command 'cmake' -ErrorAction SilentlyContinue }
    if (-not $cmakeCommand) {
        $bundled = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $bundled) { $cmakeCommand = Get-Item -LiteralPath $bundled }
    }
    if (-not $cmakeCommand) { throw '未找到 CMake。' }
    $cmakeExe = if ($cmakeCommand.Source) { $cmakeCommand.Source } else { $cmakeCommand.FullName }

    $buildDir = Join-Path $outRoot "android-$Api-arm64"
    $configureArguments = @(
        '-S', (Join-Path $repoRoot 'daemon'),
        '-B', $buildDir,
        '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DANDROID_ABI=arm64-v8a',
        "-DANDROID_PLATFORM=android-$Api",
        '-DANDROID_STL=c++_static',
        '-DCMAKE_BUILD_TYPE=Release'
    )
    & $cmakeExe @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'vcamesd Android CMake 配置失败。' }
    & $cmakeExe --build $buildDir
    if ($LASTEXITCODE -ne 0) { throw 'vcamesd Android 构建失败。' }
    $DaemonBinary = Join-Path $buildDir 'vcamesd'
}

$DaemonBinary = Resolve-ExistingFile $DaemonBinary 'vcamesd'
$KernelModule = Resolve-ExistingFile $KernelModule 'v4l2loopback module'
$ProviderBinary = Resolve-ExistingFile $ProviderBinary 'External Camera Provider'
$ReplacementAdapter = Resolve-ExistingFile $ReplacementAdapter 'Camera replacement adapter'
$CompatibilityManifest = Resolve-ExistingFile $CompatibilityManifest 'Compatibility manifest'
if ($ReplacementAdapter -and -not $CompatibilityManifest) {
    throw '打包前后摄像头替换适配器时必须提供 -CompatibilityManifest。'
}
if ($CompatibilityManifest -and -not $ReplacementAdapter) {
    throw '-CompatibilityManifest 只能与 -ReplacementAdapter 一起使用。'
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $outRoot "VCamES-Root-API$Api.zip"
}
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFull
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$standaloneOutput = Join-Path $outputDirectory 'VCamES-Root-standalone.apk'
if (Test-Path -LiteralPath $standaloneOutput) {
    Remove-Item -LiteralPath $standaloneOutput -Force
}

$stage = Join-Path $outRoot ("stage-{0}" -f [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Copy-Item -Path (Join-Path $repoRoot 'root-module\template\*') `
        -Destination $stage -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $stage 'bin') | Out-Null
    Copy-Item -LiteralPath $DaemonBinary -Destination (Join-Path $stage 'bin\vcamesd')

    $vendorEtc = Join-Path $stage 'system\vendor\etc'
    New-Item -ItemType Directory -Force -Path (Join-Path $vendorEtc 'vintf\manifest') | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'aosp\config\external_camera_config.xml') `
        -Destination (Join-Path $vendorEtc 'external_camera_config.xml')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'aosp\vintf\manifest_vcames_camera_provider.xml') `
        -Destination (Join-Path $vendorEtc 'vintf\manifest\manifest_vcames_camera_provider.xml')

    if ($KernelModule) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage 'kernel') | Out-Null
        Copy-Item -LiteralPath $KernelModule -Destination (Join-Path $stage 'kernel\v4l2loopback.ko')
    }
    if ($ProviderBinary) {
        Copy-Item -LiteralPath $ProviderBinary `
            -Destination (Join-Path $stage 'bin\external-camera-provider')
    }
    if ($ReplacementAdapter) {
        Copy-Item -LiteralPath $ReplacementAdapter `
            -Destination (Join-Path $stage 'bin\vcames-camera-adapter')
        Copy-Item -LiteralPath $CompatibilityManifest `
            -Destination (Join-Path $stage 'compatibility.properties')
    }

    # Create a controller-free module first and embed it into the Root flavor.
    # The already installed app is the controller for this installation path.
    $embeddedModule = Join-Path $generatedAssets 'vcames-root-bridge.zip'
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $embeddedModule -Force

    $builtStandalone = $false
    if ([string]::IsNullOrWhiteSpace($ControllerApk)) {
        & (Join-Path $repoRoot 'gradlew.bat') ':app:assembleRootDebug'
        if ($LASTEXITCODE -ne 0) { throw 'Root standalone APK 构建失败。' }
        $ControllerApk = Join-Path $repoRoot 'app\build\outputs\apk\root\debug\app-root-debug.apk'
        $builtStandalone = $true
    }
    $ControllerApk = Resolve-ExistingFile $ControllerApk 'Controller APK'
    Copy-Item -LiteralPath $ControllerApk -Destination (Join-Path $stage 'controller.apk')

    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $outputFull -Force
    Copy-Item -LiteralPath $ControllerApk `
        -Destination (Join-Path $outputDirectory 'VCamES-Root-controller.apk') -Force
    if ($builtStandalone) {
        Copy-Item -LiteralPath $ControllerApk `
            -Destination $standaloneOutput -Force
    }
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $resolvedOutRoot = [IO.Path]::GetFullPath($outRoot) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedStage.StartsWith($resolvedOutRoot, [StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}

Write-Host "Magisk 模块：$outputFull"
Write-Host "控制 APK：$(Join-Path $outputDirectory 'VCamES-Root-controller.apk')"
if (Test-Path -LiteralPath $standaloneOutput) {
    Write-Host "一体化 Root APK：$standaloneOutput"
}
if (-not $KernelModule) { Write-Warning '未打包 v4l2loopback.ko；设备必须已由兼容内核提供 /dev/video100。' }
if (-not $ProviderBinary) { Write-Warning '未打包 Provider；external 模式要求设备已有可启动的 external provider。' }
if (-not $ReplacementAdapter) { Write-Warning '未打包精确系统构建适配器；该 APK 只能使用 external 模式。' }
