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
$legacyRpxPath = Join-Path $buildDirectory 'PPSSPP.legacy.rpx'
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
$containerElfPath = 'build-wiiu-autoboot-docker-20230621/PPSSPP'
$legacyConvertArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $ConverterImage,
    'ext/wiiu/rpltool/rpltool',
    $containerElfPath,
    '-S',
    '-o', 'build-wiiu-autoboot-docker-20230621/PPSSPP.legacy.rpx'
)

Write-Host 'Converting PPSSPP ELF with the legacy rpltool for diagnostic A/B...'
& docker @legacyConvertArguments
if ($LASTEXITCODE -ne 0) {
    throw "Legacy PPSSPP RPX conversion failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $legacyRpxPath -PathType Leaf) -or
    (Get-Item -LiteralPath $legacyRpxPath).Length -le 0) {
    throw "Legacy RPX conversion completed without producing a valid file: $legacyRpxPath"
}

$convertArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $ConverterImage,
    '/opt/devkitpro/tools/bin/elf2rpl',
    $containerElfPath,
    'build-wiiu-autoboot-docker-20230621/PPSSPP.rpx'
)

Write-Host 'Converting PPSSPP ELF with the official elf2rpl tool...'
& docker @convertArguments
if ($LASTEXITCODE -ne 0) {
    throw "PPSSPP RPX conversion failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $rpxPath -PathType Leaf)) {
    throw "RPX conversion completed without producing: $rpxPath"
}

$symbolArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $ConverterImage,
    'sh', '-lc',
    "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-nm -P $containerElfPath | grep -E '^__SDATA(2)?_START__ '"
)
$symbolLines = @(& docker @symbolArguments)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read PPSSPP SDA symbols from the ELF (exit code $LASTEXITCODE)."
}

function Get-SdaBase {
    param(
        [Parameter(Mandatory)]
        [string]$SymbolName
    )

    $match = $symbolLines |
        Where-Object { $_ -match "^$([regex]::Escape($SymbolName))\s+\S+\s+([0-9A-Fa-f]+)\s*$" } |
        Select-Object -First 1
    if (-not $match -or $match -notmatch "^$([regex]::Escape($SymbolName))\s+\S+\s+([0-9A-Fa-f]+)\s*$") {
        throw "The PPSSPP ELF does not expose the required $SymbolName symbol."
    }

    $start = [Convert]::ToUInt32($Matches[1], 16)
    if ($start -gt ([uint32]::MaxValue - 0x8000)) {
        throw "$SymbolName cannot be converted to an SDA base."
    }

    return [uint32]($start + 0x8000)
}

$sdaBase = Get-SdaBase -SymbolName '__SDATA_START__'
$sda2Base = Get-SdaBase -SymbolName '__SDATA2_START__'
$patchResult = & (Join-Path $PSScriptRoot 'Patch-RpxSda.ps1') `
    -RpxPath $rpxPath `
    -SdaBase $sdaBase `
    -Sda2Base $sda2Base
if ($LASTEXITCODE -ne 0) {
    throw "Unable to preserve PPSSPP SDA bases in the converted RPX (exit code $LASTEXITCODE)."
}

$verifyArguments = @(
    'run',
    '--rm',
    '--network', 'none',
    '--mount', $mount,
    '-w', '/src',
    $ConverterImage,
    '/opt/devkitpro/tools/bin/readrpl',
    '-h', '-S', '-i', '-f',
    'build-wiiu-autoboot-docker-20230621/PPSSPP.rpx'
)
$verification = @(& docker @verifyArguments)
if ($LASTEXITCODE -ne 0) {
    throw "Official readrpl validation failed with exit code $LASTEXITCODE."
}

$verificationText = $verification -join "`n"
$forbiddenPatterns = @(
    'Unexpected crc',
    'Unsupported relocation',
    'Error reading section',
    'extends past the end'
)
foreach ($pattern in $forbiddenPatterns) {
    if ($verificationText -match $pattern) {
        throw "The converted PPSSPP RPX failed readrpl verification: $pattern"
    }
}

$requiredPatterns = @(
    'abi\s+=\s+EABI_CAFE',
    'type\s+=\s+65025\s+0xFE01',
    '\.text\s+SHT_PROGBITS',
    '\.fimport_coreinit\s+SHT_RPL_IMPORTS',
    '\.fimport_proc_ui\s+SHT_RPL_IMPORTS',
    '\.fimport_sysapp\s+SHT_RPL_IMPORTS',
    "sdaBase\s+=\s+0x$($sdaBase.ToString('X8'))",
    "sda2Base\s+=\s+0x$($sda2Base.ToString('X8'))",
    'sdkVersion\s+=\s+0x5335',
    'sdkRevision\s+=\s+0x10D4B'
)
foreach ($pattern in $requiredPatterns) {
    if ($verificationText -notmatch $pattern) {
        throw "The converted PPSSPP RPX failed contract check: $pattern"
    }
}

$entryMatch = [regex]::Match(
    $verificationText,
    '(?m)^\s*entry\s+=\s+0x([0-9A-Fa-f]{8})\s*$')
$textMatch = [regex]::Match(
    $verificationText,
    '(?m)^\s*\[\s*\d+\]\s+\.text\s+\S+\s+([0-9A-Fa-f]{8})\s+')
$textSizeMatch = [regex]::Match(
    $verificationText,
    '(?m)^\s*textSize\s+=\s+0x([0-9A-Fa-f]{8})\s*$')
if (-not $entryMatch.Success -or -not $textMatch.Success -or -not $textSizeMatch.Success) {
    throw 'The converted PPSSPP RPX did not expose a parseable entry point and text memory span.'
}

$entryAddress = [Convert]::ToUInt32($entryMatch.Groups[1].Value, 16)
$textAddress = [Convert]::ToUInt32($textMatch.Groups[1].Value, 16)
$textSize = [Convert]::ToUInt32($textSizeMatch.Groups[1].Value, 16)
$textEnd = [uint64]$textAddress + [uint64]$textSize
if ($entryAddress -lt $textAddress -or [uint64]$entryAddress -ge $textEnd) {
    throw "The RPX entry point 0x$($entryAddress.ToString('X8')) is outside the text memory span."
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
$legacyRpxHash = if (Test-Path -LiteralPath $legacyRpxPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $legacyRpxPath).Hash
} else {
    throw 'The diagnostic legacy RPX is missing.'
}
if ($legacyRpxHash -eq $rpxHash) {
    throw 'The primary and legacy RPX hashes are identical; the converter A/B is invalid.'
}
$manifest = [ordered]@{
    schemaVersion = 2
    runtime = 'UInjectForge PPSSPP Wii U diagnostic runtime'
    sourceCommit = $sourceCommit
    sourceDirty = $sourceDirty
    builtAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    builderImage = $BuilderImage
    converterImage = $ConverterImage
    converter = 'devkitPro wut-tools elf2rpl with preserved SDA bases'
    elfSha256 = $elfHash
    rpxSha256 = $rpxHash
    legacyRpxSha256 = $legacyRpxHash
    sdaBase = "0x$($sdaBase.ToString('X8'))"
    sda2Base = "0x$($sda2Base.ToString('X8'))"
    capabilities = @(
        'package-content-v1',
        'menu-probe-v1',
        'complete-assets-v1',
        'early-entry-log-v2',
        'heap-failure-reporting-v1',
        'persistent-exception-log-v1',
        'debug-tls-free-v1',
        'official-elf2rpl-v1',
        'preserved-sda-fileinfo-v1'
    )
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "ELF SHA-256: $elfHash"
Write-Host "RPX SHA-256: $rpxHash"
if ($legacyRpxHash) {
    Write-Host "Legacy RPX SHA-256 (A/B only): $legacyRpxHash"
}
Write-Host "SDA bases: 0x$($sdaBase.ToString('X8')), 0x$($sda2Base.ToString('X8'))"
Write-Host "Runtime manifest: $manifestPath"

[pscustomobject]@{
    ElfPath = $elfPath
    RpxPath = $rpxPath
    ManifestPath = $manifestPath
    RpxSha256 = $rpxHash
}
