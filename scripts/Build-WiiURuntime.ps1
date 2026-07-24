[CmdletBinding()]
param(
    [string]$BuilderImage = 'ghcr.io/wiiu-env/devkitppc:20230621',
    [string]$ConverterImage = 'devkitpro/devkitppc:latest',
    [ValidateRange(1, 32)]
    [int]$Parallel = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = Join-Path $repoRoot 'build-wiiu-autoboot-docker-20230621'
$elfPath = Join-Path $buildDirectory 'PPSSPP'
$rpxPath = Join-Path $buildDirectory 'PPSSPP.rpx'
$manifestPath = Join-Path $buildDirectory 'PPSSPP.runtime.json'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker is required to build the Wii U runtime.'
}
if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -PathType Leaf)) {
    throw "The configured Wii U build directory was not found: $buildDirectory"
}

$mount = "type=bind,source=$repoRoot,target=/src"
$buildArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $BuilderImage,
    'cmake', '--build', 'build-wiiu-autoboot-docker-20230621',
    '--parallel', $Parallel.ToString([System.Globalization.CultureInfo]::InvariantCulture)
)

Write-Host 'Building PPSSPP Wii U ELF...'
& docker @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "PPSSPP Wii U ELF build failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "The build completed without producing the expected ELF: $elfPath"
}

$elf = Get-Item -LiteralPath $elfPath
$convertArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $ConverterImage,
    'ext/wiiu/rpltool/rpltool',
    'build-wiiu-autoboot-docker-20230621/PPSSPP',
    '-S',
    '-o', 'build-wiiu-autoboot-docker-20230621/PPSSPP.rpx'
)

Write-Host 'Converting PPSSPP ELF to RPX...'
& docker @convertArguments
if ($LASTEXITCODE -ne 0) {
    throw "PPSSPP RPX conversion failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $rpxPath -PathType Leaf)) {
    throw "RPX conversion completed without producing: $rpxPath"
}

$rpx = Get-Item -LiteralPath $rpxPath
if ($rpx.Length -le 0) {
    throw 'RPX conversion produced an empty file.'
}
if ($rpx.LastWriteTimeUtc -lt $elf.LastWriteTimeUtc) {
    throw "The RPX is older than the ELF. Refusing to publish a stale runtime: $rpxPath"
}

$sourceCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[a-f0-9]{40}$') {
    throw 'Unable to determine the PPSSPP source commit.'
}
$sourceDirty = [bool](& git -C $repoRoot status --porcelain)
$elfHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $elfPath).Hash
$rpxHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $rpxPath).Hash
$manifest = [ordered]@{
    schemaVersion = 1
    runtime = 'UInjectForge PPSSPP Wii U diagnostic runtime'
    sourceCommit = $sourceCommit
    sourceDirty = $sourceDirty
    builtAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    builderImage = $BuilderImage
    converterImage = $ConverterImage
    elfSha256 = $elfHash
    rpxSha256 = $rpxHash
    capabilities = @(
        'package-content-v1',
        'menu-probe-v1',
        'complete-assets-v1',
        'early-entry-log-v2',
        'heap-failure-reporting-v1',
        'persistent-exception-log-v1'
    )
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "ELF SHA-256: $elfHash"
Write-Host "RPX SHA-256: $rpxHash"
Write-Host "Runtime manifest: $manifestPath"

[pscustomobject]@{
    ElfPath = $elfPath
    RpxPath = $rpxPath
    ManifestPath = $manifestPath
    RpxSha256 = $rpxHash
}
