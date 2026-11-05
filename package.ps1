param(
    [string]$Version = '0.1.2',
    [string]$SourceBin = ''
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'La versión debe tener formato X.Y.Z' }
$workspacePath = [IO.Path]::GetFullPath($PSScriptRoot)
if (!$SourceBin) { $SourceBin = Join-Path $workspacePath 'out\bin' }
$qtBin = 'C:\Qt\6.7.0\mingw_64\bin'
$packageName = 'forgecraft_' + $Version.Replace('.', '_')
$stage = Join-Path $workspacePath "out\$packageName"
$packages = Join-Path $workspacePath 'out\packages'
New-Item -ItemType Directory -Path $packages -Force | Out-Null
if (Test-Path -LiteralPath $stage) {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $archive = Join-Path $packages ($packageName + '-previous-' + [guid]::NewGuid().ToString('N'))
    if (!$resolvedStage.StartsWith($workspacePath + '\out\') -or ![IO.Path]::GetFullPath($archive).StartsWith($packages + '\')) { throw 'Ruta de paquete no válida' }
    Move-Item -LiteralPath $resolvedStage -Destination $archive
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $SourceBin 'forgecraft.exe'), (Join-Path $SourceBin 'forgecraft.qss') -Destination $stage
& "$qtBin\windeployqt.exe" --release --compiler-runtime --no-translations --dir $stage (Join-Path $stage 'forgecraft.exe')
if ($LASTEXITCODE) { throw 'Falló el despliegue de Qt' }
# Plugins que la aplicación necesita además de los que copia windeployqt:
# iconos SVG (qsvgicon), lectura de SVG (qsvg) y la plataforma de las pruebas.
foreach ($plugin in @(@('iconengines', 'qsvgicon.dll'), @('imageformats', 'qsvg.dll'), @('platforms', 'qoffscreen.dll'))) {
    $target = Join-Path $stage $plugin[0]
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $qtBin "..\plugins\$($plugin[0])\$($plugin[1])") -Destination $target -Force
}
# Códecs MP3 que el proyecto distribuye sin modificar.
Copy-Item -LiteralPath (Join-Path $workspacePath 'svn\third_party\mp3\bin\libmpg123-0.dll'), (Join-Path $workspacePath 'svn\third_party\mp3\bin\libmp3lame-0.dll') -Destination $stage -Force
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $workspacePath 'LICENSE') -Destination (Join-Path $licenses 'ForgeCraft-GPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $workspacePath 'svn\distribution\THIRD_PARTY_NOTICES.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $workspacePath 'svn\distribution\README-WINDOWS.txt') -Destination (Join-Path $stage 'README.txt')
foreach ($licenseFile in Get-ChildItem -LiteralPath (Join-Path $workspacePath 'svn\distribution\licenses') -File) {
    Copy-Item -LiteralPath $licenseFile.FullName -Destination $licenses
}
$zip = Join-Path $packages ($packageName + '.zip')
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal -Force
$digest = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $packages 'SHA256SUMS.txt'), "$digest  $packageName.zip`n", [Text.UTF8Encoding]::new($false))
Write-Output "PACKAGE=$zip"
Write-Output "SHA256=$digest"
