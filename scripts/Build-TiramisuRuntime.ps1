[CmdletBinding()]
param(
    [string]$BuilderImage = 'ghcr.io/wiiu-env/devkitppc:20230621',
    [string]$ConverterImage = 'devkitpro/devkitppc:latest',
    [ValidateRange(1, 32)][int]$Parallel = 4
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildName = 'build-wiiu-autoboot-docker-20230621'
& docker run --rm --network none --mount "type=bind,source=$root,target=/src" -w /src $BuilderImage cmake -S . -B $buildName '-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/wiiu.cmake' '-DCMAKE_BUILD_TYPE=Release' '-DWIIU_SKIP_RPLTOOL=ON' '-DWIIU_USE_SYSTEM_THREADS=OFF'
if ($LASTEXITCODE -ne 0) { throw "Wii U configure failed ($LASTEXITCODE)." }
& (Join-Path $PSScriptRoot 'Build-WiiURuntime.ps1') -BuilderImage $BuilderImage -ConverterImage $ConverterImage -BuildDirectoryName $buildName -RuntimeVariant TiramisuControl -SkipLegacyConverter -Parallel $Parallel -AdditionalCapabilities @('gx2-compatibility-v1', 'legacy-devkitppc-20230621-v1', 'tiramisu-control-v1')
