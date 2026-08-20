[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [ValidateSet('File', 'Dshow')]
    [string]$Mode = 'File',

    [ValidateRange(160, 3840)]
    [int]$Width = 1280,

    [ValidateRange(120, 2160)]
    [int]$Height = 720,

    [ValidateRange(1, 60)]
    [int]$Fps = 30,

    [ValidateRange(1, 31)]
    [int]$Quality = 3,

    [ValidateRange(1024, 65535)]
    [int]$Port = 8888,

    [switch]$Restart
)

$ErrorActionPreference = 'Stop'
$ffmpeg = (Get-Command 'ffmpeg.exe' -ErrorAction Stop).Source
if ($Mode -eq 'File' -and -not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "找不到视频文件：$Source"
}

$inputArguments = if ($Mode -eq 'Dshow') {
    @('-f', 'dshow', '-i', "video=$Source")
} else {
    @('-re', '-stream_loop', '-1', '-i', (Resolve-Path -LiteralPath $Source).Path)
}
$filter = "fps=$Fps,scale=${Width}:${Height}:force_original_aspect_ratio=increase," +
    "crop=${Width}:${Height},setsar=1"
$endpoint = "http://0.0.0.0:$Port/live.mjpg"
$arguments = @(
    '-hide_banner', '-loglevel', 'warning'
) + $inputArguments + @(
    '-map', '0:v:0', '-an', '-sn', '-dn'
    '-vf', $filter
    '-c:v', 'mjpeg', '-pix_fmt', 'yuvj420p', '-q:v', [string]$Quality
    '-fps_mode', 'passthrough', '-flush_packets', '1'
    '-f', 'fifo', '-fifo_format', 'mpjpeg', '-queue_size', '8'
    '-drop_pkts_on_overflow', '1'
    '-format_opts', 'listen=1:flush_packets=1:tcp_nodelay=1:content_type=multipart/x-mixed-replace;boundary=ffmpeg'
    $endpoint
)

$addresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.AddressState -eq 'Preferred' } |
    Select-Object -ExpandProperty IPAddress -Unique)
Write-Host "VCamES MJPEG："
foreach ($address in $addresses) {
    Write-Host "  http://${address}:$Port/live.mjpg"
}
Write-Host '按 Ctrl+C 停止。Windows 防火墙需允许此 TCP 端口，仅在可信局域网使用。'

do {
    & $ffmpeg @arguments
    $exitCode = $LASTEXITCODE
    if (-not $Restart -or $exitCode -eq 0) {
        exit $exitCode
    }
    Write-Warning "FFmpeg 已退出（$exitCode），1 秒后重启。"
    Start-Sleep -Seconds 1
} while ($true)
