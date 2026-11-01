param([switch]$Deploy, [switch]$Test)
$ErrorActionPreference = 'Stop'
$qtBin = 'C:\Qt\6.7.0\mingw_64\bin'
$compilerBin = 'C:\Qt\Tools\mingw1120_64\bin'
$runtimeDirs = @('C:\msys64\ucrt64\bin', 'C:\msys64\mingw64\bin') | Where-Object { Test-Path -LiteralPath $_ }
$env:PATH = "$compilerBin;$qtBin;" + ($runtimeDirs -join ';') + ';' + $env:PATH
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $projectRoot 'out\build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
# Keep compatible assembler/linker tools private to this build. The C++ compiler
# and Qt ABI still come from the same Qt 6.7 / MinGW 11.2 kit used by Radit.
$candidates = @('C:\msys64\ucrt64\bin', 'C:\msys64\mingw64\bin', (Join-Path $projectRoot '..\radit\out\qt67-binutils'))
$replacement = $candidates | Where-Object { (Test-Path -LiteralPath (Join-Path $_ 'as.exe')) -and (Test-Path -LiteralPath (Join-Path $_ 'ld.exe')) } | Select-Object -First 1
$toolArgs = @()
if ($replacement) {
    $localTools = Join-Path $buildDir 'tools'
    New-Item -ItemType Directory -Path $localTools -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $replacement 'as.exe'), (Join-Path $replacement 'ld.exe') -Destination $localTools -Force
    foreach ($dll in @('libintl-8.dll', 'libiconv-2.dll', 'zlib1.dll', 'libzstd.dll')) {
        $dependency = Join-Path $replacement $dll
        if (Test-Path -LiteralPath $dependency) { Copy-Item -LiteralPath $dependency -Destination $localTools -Force }
    }
    # Antes de adoptar esas herramientas, comprobar que enlazan: algunas compilaciones
    # de binutils de MSYS2 fallan con "lseek: Bad file descriptor" al leer crt2.o.
    # Si la comprobación falla, se usan las herramientas del kit de Qt.
    Push-Location $buildDir
    try {
        Set-Content -LiteralPath 'tool-probe.cpp' -Value 'int main() { return 0; }' -Encoding ascii
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & "$compilerBin\g++.exe" '-Btools/' 'tool-probe.cpp' '-o' 'tool-probe.exe' 2>&1 | Out-Null
            $links = ($LASTEXITCODE -eq 0) -and (Test-Path -LiteralPath 'tool-probe.exe')
        } catch { $links = $false } finally { $ErrorActionPreference = $previous }
        if ($links) {
            $toolArgs = @('QMAKE_CXXFLAGS+=-Btools/', 'QMAKE_LFLAGS+=-Btools/')
        } else {
            Write-Warning "Las herramientas de $replacement no enlazan; se usan las del kit de Qt."
            $toolArgs = @()
        }
        Remove-Item -LiteralPath 'tool-probe.cpp', 'tool-probe.exe' -Force -ErrorAction SilentlyContinue
    } finally { Pop-Location }
}
Push-Location $buildDir
try {
    & "$qtBin\qmake.exe" (Join-Path $PSScriptRoot 'forgecraft.pro') 'CONFIG+=release' 'CONFIG-=debug' @toolArgs
    if ($LASTEXITCODE) { throw 'qmake failed' }
    & "$compilerBin\mingw32-make.exe" '-j4'
    if ($LASTEXITCODE) { throw 'Build failed' }
    $exe = Join-Path $projectRoot 'out\bin\forgecraft.exe'
    if ($Deploy) {
        $destination = Split-Path $exe
        foreach ($dll in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Concurrent.dll', 'Qt6Svg.dll')) {
            Copy-Item -LiteralPath (Join-Path $qtBin $dll) -Destination $destination -Force
        }
        foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
            Copy-Item -LiteralPath (Join-Path $compilerBin $dll) -Destination $destination -Force
        }
        foreach ($dll in @('libmpg123-0.dll', 'libmp3lame-0.dll')) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot "third_party\mp3\bin\$dll") -Destination $destination -Force
        }
        $licenses = Join-Path $destination 'licenses'
        New-Item -ItemType Directory -Path $licenses -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\mp3\licenses\COPYING.txt'), (Join-Path $PSScriptRoot 'third_party\mp3\NOTICE.md') -Destination $licenses -Force
        $platforms = Join-Path $destination 'platforms'
        New-Item -ItemType Directory -Path $platforms -Force | Out-Null
        foreach ($dll in @('qwindows.dll', 'qoffscreen.dll')) {
            Copy-Item -LiteralPath (Join-Path $qtBin "..\plugins\platforms\$dll") -Destination $platforms -Force
        }
        # La hoja de estilo viaja junto al ejecutable para poder ajustar el tema
        # sin recompilar. Si falta, main.cpp usa la copia incrustada en el .qrc.
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'style\forgecraft.qss') -Destination $destination -Force
        # Motor de iconos SVG: sin este plugin los iconos de la interfaz no se pintan.
        $iconEngines = Join-Path $destination 'iconengines'
        New-Item -ItemType Directory -Path $iconEngines -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $qtBin '..\plugins\iconengines\qsvgicon.dll') -Destination $iconEngines -Force
        $imageFormats = Join-Path $destination 'imageformats'
        New-Item -ItemType Directory -Path $imageFormats -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $qtBin '..\plugins\imageformats\qsvg.dll') -Destination $imageFormats -Force
    }
    if ($Test) {
        $process = Start-Process -FilePath $exe -ArgumentList '--self-test', '-platform', 'offscreen' -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $buildDir 'test-results.txt') -RedirectStandardError (Join-Path $buildDir 'test-errors.txt')
        Get-Content (Join-Path $buildDir 'test-results.txt')
        if ($process.ExitCode) { throw 'Audio tests failed' }
        $process = Start-Process -FilePath $exe -ArgumentList '--smoke-test' -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $buildDir 'ui-results.txt') -RedirectStandardError (Join-Path $buildDir 'ui-errors.txt')
        Get-Content (Join-Path $buildDir 'ui-results.txt')
        if ($process.ExitCode) { throw 'UI tests failed' }
    }
} finally { Pop-Location }
