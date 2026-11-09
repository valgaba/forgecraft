param([string]$Archive = '', [string]$Destination = '')
$ErrorActionPreference = 'Stop'
$expectedHash = '4705843ccaaf54257c16ad90f3e952ece33c17df964ecf7bfdbb0f49c7171077'
$downloadDir = Join-Path $PSScriptRoot 'out\codec-download'
New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null
if (!$Archive) {
    $Archive = Join-Path $downloadDir 'ffmpeg-release-essentials.7z'
    if (!(Test-Path -LiteralPath $Archive)) {
        Invoke-WebRequest -Uri 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.7z' -OutFile $Archive -UseBasicParsing
    }
}
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'El archivo no corresponde al FFmpeg 9.0.2 verificado. Usa -Archive con la versión indicada en svn/third_party/ffmpeg/NOTICE.md. No se instala una versión diferente automáticamente.'
}
$sevenZip = @('C:\Program Files\7-Zip\7z.exe', 'C:\Program Files (x86)\7-Zip\7z.exe') | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$sevenZip) { throw 'Se necesita 7-Zip para extraer el archivo verificado.' }
$extract = Join-Path $downloadDir ('verified-' + [guid]::NewGuid().ToString('N'))
& $sevenZip x $Archive "-o$extract" -y | Out-Null
if ($LASTEXITCODE) { throw 'No se pudo extraer FFmpeg.' }
$upstream = Join-Path $extract 'ffmpeg-9.0.2-essentials_build'
$vendor = Join-Path $PSScriptRoot 'svn\third_party\ffmpeg'
$vendorBin = Join-Path $vendor 'bin'
if (!$Destination) { $Destination = Join-Path $PSScriptRoot 'out\bin\codecs' }
New-Item -ItemType Directory -Path $vendorBin, $Destination -Force | Out-Null
foreach ($name in @('ffmpeg.exe', 'ffprobe.exe')) {
    $binary = Join-Path $upstream "bin\$name"
    Copy-Item -LiteralPath $binary -Destination $vendorBin -Force
    Copy-Item -LiteralPath $binary -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $upstream 'LICENSE') -Destination (Join-Path $vendor 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $upstream 'README.txt') -Destination (Join-Path $vendor 'BUILD-README.txt') -Force
Write-Output "FFmpeg 9.0.2 instalado en $Destination"
