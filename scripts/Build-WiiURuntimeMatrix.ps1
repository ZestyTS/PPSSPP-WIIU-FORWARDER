[CmdletBinding()]
param(
    [string]$LegacyBuilderImage = 'ghcr.io/wiiu-env/devkitppc:20230621',
    [string]$AromaBuilderImage = 'devkitpro/devkitppc:latest',
    [string]$ConverterImage = 'devkitpro/devkitppc:latest',
    [ValidateRange(1, 32)]
    [int]$Parallel = 4,
    [string]$OutputDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtimeBuilder = Join-Path $PSScriptRoot 'Build-WiiURuntime.ps1'
$legacyBuildDirectoryName = 'build-wiiu-autoboot-docker-20230621'
$aromaBuildDirectoryName = 'build-wiiu-aroma-latest'
$mount = "type=bind,source=$repoRoot,target=/src"

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker is required to build the Wii U runtime matrix.'
}

function Initialize-WiiUBuildDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$BuilderImage,
        [Parameter(Mandatory)]
        [string]$BuildDirectoryName,
        [Parameter(Mandatory)]
        [ValidateSet('ON', 'OFF')]
        [string]$UseSystemThreads
    )

    $configureArguments = @(
        'run',
        '--rm',
        '--network', 'none',
        '--mount', $mount,
        '-w', '/src',
        $BuilderImage,
        'cmake',
        '-S', '.',
        '-B', $BuildDirectoryName,
        '-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/wiiu.cmake',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DWIIU_SKIP_RPLTOOL=ON',
        "-DWIIU_USE_SYSTEM_THREADS=$UseSystemThreads"
    )

    Write-Host "Configuring $BuildDirectoryName with $BuilderImage..."
    & docker @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure $BuildDirectoryName (exit code $LASTEXITCODE)."
    }
}

Initialize-WiiUBuildDirectory `
    -BuilderImage $LegacyBuilderImage `
    -BuildDirectoryName $legacyBuildDirectoryName `
    -UseSystemThreads OFF
Initialize-WiiUBuildDirectory `
    -BuilderImage $AromaBuilderImage `
    -BuildDirectoryName $aromaBuildDirectoryName `
    -UseSystemThreads ON

& $runtimeBuilder `
    -BuilderImage $LegacyBuilderImage `
    -ConverterImage $ConverterImage `
    -BuildDirectoryName $legacyBuildDirectoryName `
    -RuntimeVariant TiramisuControl `
    -AdditionalCapabilities @(
        'gx2-compatibility-v1',
        'legacy-devkitppc-20230621-v1',
        'tiramisu-control-v1'
    ) `
    -Parallel $Parallel

& $runtimeBuilder `
    -BuilderImage $AromaBuilderImage `
    -ConverterImage $ConverterImage `
    -BuildDirectoryName $aromaBuildDirectoryName `
    -RuntimeVariant Aroma `
    -SkipLegacyConverter `
    -AdditionalCapabilities @(
        'aroma-current-toolchain-v1',
        'gx2-compatibility-v1',
        'system-pthread-v1'
    ) `
    -Parallel $Parallel

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputDirectory = Join-Path $repoRoot "publish/PPSSPP-WiiU-Aroma-matrix-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Publish-RuntimeVariant {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [Parameter(Mandatory)]
        [string]$BuildDirectoryName
    )

    $buildDirectory = Join-Path $repoRoot $BuildDirectoryName
    $sourceRpx = Join-Path $buildDirectory 'PPSSPP.rpx'
    $sourceManifest = Join-Path $buildDirectory 'PPSSPP.runtime.json'
    $sourceAssets = Join-Path $buildDirectory 'assets'
    foreach ($requiredPath in @($sourceRpx, $sourceManifest, $sourceAssets)) {
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            throw "Runtime matrix input is missing: $requiredPath"
        }
    }

    $destination = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $sourceRpx -Destination (Join-Path $destination 'PPSSPP.rpx') -Force
    Copy-Item -LiteralPath $sourceManifest -Destination (Join-Path $destination 'PPSSPP.runtime.json') -Force
    Copy-Item -LiteralPath $sourceAssets -Destination (Join-Path $destination 'assets') -Recurse -Force

    $manifest = Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
    return [ordered]@{
        name = $Name
        runtimeVariant = $manifest.runtimeVariant
        rpx = "$Name/PPSSPP.rpx"
        manifest = "$Name/PPSSPP.runtime.json"
        rpxSha256 = $manifest.rpxSha256
        builderImage = $manifest.builderImage
        capabilities = @($manifest.capabilities)
    }
}

$variants = @(
    Publish-RuntimeVariant -Name 'Aroma' -BuildDirectoryName $aromaBuildDirectoryName
    Publish-RuntimeVariant -Name 'Tiramisu-Control' -BuildDirectoryName $legacyBuildDirectoryName
)
$sourceCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$matrixManifest = [ordered]@{
    schemaVersion = 1
    sourceCommit = $sourceCommit
    generatedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    purpose = 'A/B test current Aroma toolchain behavior against the hardware-proven Tiramisu baseline.'
    variants = $variants
}
$matrixManifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'runtime-matrix.json') -Encoding UTF8

$readme = @"
PPSSPP Wii U Aroma A/B runtime matrix

1. Start with Aroma/PPSSPP.rpx in UInjectForge.
2. Keep its assets folder beside the selected RPX.
3. Build with Launch to PPSSPP menu only and the Compatibility profile first.
4. If Aroma does not open, repeat the same build using Tiramisu-Control/PPSSPP.rpx.
5. Do not change the game, artwork, title ID, or other runtime settings between the two builds.

The Tiramisu-Control folder is a diagnostic baseline. The Aroma folder uses the current devkitPPC pthread/runtime path.
"@
Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.txt') -Value $readme -Encoding UTF8

$hashLines = foreach ($variant in $variants) {
    "$($variant.rpxSha256)  $($variant.rpx)"
}
Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Value $hashLines -Encoding ASCII

$zipPath = "$OutputDirectory.zip"
Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash

Write-Host "Runtime matrix: $OutputDirectory"
Write-Host "Archive: $zipPath"
Write-Host "Archive SHA-256: $zipHash"

[pscustomobject]@{
    OutputDirectory = $OutputDirectory
    ZipPath = $zipPath
    ZipSha256 = $zipHash
}
