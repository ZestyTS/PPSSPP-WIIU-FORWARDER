[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$RpxPath,

    [Parameter(Mandatory)]
    [uint32]$SdaBase,

    [Parameter(Mandatory)]
    [uint32]$Sda2Base
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

[uint32]$CrcInitial = 4294967295L
[uint32]$CrcPolynomial = 3988292384L
[uint32]$RplCrcSectionType = 2147483651L
[uint32]$RplFileInfoSectionType = 2147483652L

function Get-U16BigEndian {
    param([byte[]]$Buffer, [int]$Offset)

    return [uint16](
        ([uint16]$Buffer[$Offset] -shl 8) -bor
        [uint16]$Buffer[$Offset + 1])
}

function Get-U32BigEndian {
    param([byte[]]$Buffer, [int]$Offset)

    return [uint32](
        ([uint32]$Buffer[$Offset] -shl 24) -bor
        ([uint32]$Buffer[$Offset + 1] -shl 16) -bor
        ([uint32]$Buffer[$Offset + 2] -shl 8) -bor
        [uint32]$Buffer[$Offset + 3])
}

function Set-U32BigEndian {
    param([byte[]]$Buffer, [int]$Offset, [uint32]$Value)

    $Buffer[$Offset] = [byte](($Value -shr 24) -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 3] = [byte]($Value -band 0xFF)
}

function Get-Crc32 {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [int]$Length
    )

    [uint32]$crc = $CrcInitial
    for ($index = $Offset; $index -lt ($Offset + $Length); $index++) {
        $crc = [uint32]($crc -bxor [uint32]$Buffer[$index])
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor $CrcPolynomial)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }

    return [uint32]($crc -bxor $CrcInitial)
}

$resolvedPath = [System.IO.Path]::GetFullPath($RpxPath)
if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
    throw "RPX was not found: $resolvedPath"
}

$bytes = [System.IO.File]::ReadAllBytes($resolvedPath)
if ($bytes.Length -lt 0x34) {
    throw 'RPX is too small to contain an ELF32 header.'
}
if ($bytes[0] -ne 0x7F -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4C -or $bytes[3] -ne 0x46) {
    throw 'RPX does not have an ELF magic header.'
}
if ($bytes[4] -ne 1 -or $bytes[5] -ne 2) {
    throw 'RPX must be an ELF32 big-endian file.'
}
if ((Get-U16BigEndian $bytes 0x10) -ne 0xFE01) {
    throw 'RPX does not have the Wii U Cafe executable type.'
}

$sectionHeaderOffset = [int64](Get-U32BigEndian $bytes 0x20)
$sectionHeaderSize = [int](Get-U16BigEndian $bytes 0x2E)
$sectionCount = [int](Get-U16BigEndian $bytes 0x30)
if ($sectionHeaderSize -lt 40 -or $sectionCount -lt 1) {
    throw 'RPX section-header table is invalid.'
}

$sectionTableEnd = $sectionHeaderOffset + ([int64]$sectionHeaderSize * $sectionCount)
if ($sectionHeaderOffset -lt 0 -or $sectionTableEnd -gt $bytes.LongLength) {
    throw 'RPX section-header table extends beyond the file.'
}

$crcOffset = -1
$crcSize = -1
$fileInfoOffset = -1
$fileInfoSize = -1
$fileInfoIndex = -1
for ($sectionIndex = 0; $sectionIndex -lt $sectionCount; $sectionIndex++) {
    $headerOffset = [int]($sectionHeaderOffset + ([int64]$sectionHeaderSize * $sectionIndex))
    $sectionType = Get-U32BigEndian $bytes ($headerOffset + 4)
    $sectionOffset = [int64](Get-U32BigEndian $bytes ($headerOffset + 16))
    $sectionSize = [int64](Get-U32BigEndian $bytes ($headerOffset + 20))

    if ($sectionOffset -lt 0 -or $sectionSize -lt 0 -or
        ($sectionSize -gt 0 -and ($sectionOffset + $sectionSize) -gt $bytes.LongLength)) {
        throw "RPX section $sectionIndex extends beyond the file."
    }

    if ($sectionType -eq $RplCrcSectionType) {
        $crcOffset = [int]$sectionOffset
        $crcSize = [int]$sectionSize
    } elseif ($sectionType -eq $RplFileInfoSectionType) {
        $fileInfoOffset = [int]$sectionOffset
        $fileInfoSize = [int]$sectionSize
        $fileInfoIndex = $sectionIndex
    }
}

if ($fileInfoOffset -lt 0 -or $fileInfoSize -ne 0x60) {
    throw 'RPX does not contain a valid 0x60-byte RPL file-info section.'
}
if ($crcOffset -lt 0 -or $crcSize -lt (($fileInfoIndex + 1) * 4)) {
    throw 'RPX does not contain a complete RPL CRC table.'
}

Set-U32BigEndian $bytes ($fileInfoOffset + 0x24) $SdaBase
Set-U32BigEndian $bytes ($fileInfoOffset + 0x28) $Sda2Base
$fileInfoCrc = Get-Crc32 $bytes $fileInfoOffset $fileInfoSize
Set-U32BigEndian $bytes ($crcOffset + ($fileInfoIndex * 4)) $fileInfoCrc

[System.IO.File]::WriteAllBytes($resolvedPath, $bytes)

[pscustomobject]@{
    RpxPath = $resolvedPath
    SdaBase = "0x$($SdaBase.ToString('X8'))"
    Sda2Base = "0x$($Sda2Base.ToString('X8'))"
    FileInfoCrc32 = "0x$($fileInfoCrc.ToString('X8'))"
}
