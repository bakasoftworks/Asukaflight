[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$FirmwarePath,
    [string]$RepoRoot = "",
    [switch]$PruneOtherRootGx12Firmware
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

$repoRootFull = [System.IO.Path]::GetFullPath($RepoRoot)
$firmwarePathFull = if ([System.IO.Path]::IsPathRooted($FirmwarePath)) {
    [System.IO.Path]::GetFullPath($FirmwarePath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRootFull $FirmwarePath))
}

if (-not (Test-Path -LiteralPath $firmwarePathFull -PathType Leaf)) {
    throw "Firmware file not found: $firmwarePathFull"
}

$firmwareFileName = Split-Path -Leaf $firmwarePathFull
if ($firmwareFileName -notmatch '^R2X-[0-9A-Fa-f]{3}\.BIN$') {
    throw "Firmware filename must use the GX12 short-name form R2X-XXX.BIN: $firmwareFileName"
}

$firmwareHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $firmwarePathFull).Hash.ToUpperInvariant()
$firmwareSizeBytes = (Get-Item -LiteralPath $firmwarePathFull).Length
$firmwareSizeText = "{0:N0}" -f $firmwareSizeBytes
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-RepoFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return Join-Path $script:repoRootFull $RelativePath
}

function Read-RepoFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Resolve-RepoFile $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }

    return [System.IO.File]::ReadAllText($path)
}

function Update-RepoFile {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][scriptblock]$Updater
    )

    $path = Resolve-RepoFile $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }

    $original = [System.IO.File]::ReadAllText($path)
    $updated = & $Updater $original
    if ($updated -eq $null) {
        throw "Updater returned null for $RelativePath"
    }

    if ($updated -ne $original) {
        if ($PSCmdlet.ShouldProcess($path, "Update GX12 release firmware references")) {
            [System.IO.File]::WriteAllText($path, $updated, $script:utf8NoBom)
        }
        Write-Host "updated $RelativePath"
    } else {
        Write-Host "unchanged $RelativePath"
    }
}

function Replace-Gx12FirmwareName {
    param([Parameter(Mandatory = $true)][string]$Text)
    return [regex]::Replace($Text, 'R2X-[0-9A-Fa-f]{3}\.BIN', $script:firmwareFileName)
}

function Replace-PinnedFirmwareHash {
    param([Parameter(Mandatory = $true)][string]$Text)
    return $Text.Replace($script:currentFirmwareHash, $script:firmwareHash)
}

$publisherText = Read-RepoFile "tools\publish-gx12-distribution.ps1"
$currentNameMatch = [regex]::Match($publisherText, 'FirmwareSdFileName\s*=\s*"(?<name>R2X-[0-9A-Fa-f]{3}\.BIN)"')
$currentHashMatch = [regex]::Match($publisherText, 'expectedFirmwareHash\s*=\s*"(?<hash>[0-9A-Fa-f]{64})"')

if (-not $currentNameMatch.Success) {
    throw "Could not find current FirmwareSdFileName in tools\publish-gx12-distribution.ps1"
}

if (-not $currentHashMatch.Success) {
    throw "Could not find current expectedFirmwareHash in tools\publish-gx12-distribution.ps1"
}

$script:currentFirmwareName = $currentNameMatch.Groups["name"].Value
$script:currentFirmwareHash = $currentHashMatch.Groups["hash"].Value.ToUpperInvariant()
$script:firmwareFileName = $firmwareFileName
$script:firmwareHash = $firmwareHash
$script:firmwareSizeText = $firmwareSizeText
$script:utf8NoBom = $utf8NoBom

Update-RepoFile ".gitignore" {
    param($text)
    $text -replace '!firmware/R2X-[0-9A-Fa-f]{3}\.BIN', "!firmware/$script:firmwareFileName"
}

foreach ($file in @(
    "README.md",
    "docs\release-packages.md",
    "firmware\README.md"
)) {
    Update-RepoFile $file {
        param($text)
        Replace-Gx12FirmwareName $text
    }
}

Update-RepoFile "docs\licensing.md" {
    param($text)
    Replace-PinnedFirmwareHash (Replace-Gx12FirmwareName $text)
}

Update-RepoFile "docs\firmware-source.md" {
    param($text)
    $updated = Replace-PinnedFirmwareHash (Replace-Gx12FirmwareName $text)
    $updated = [regex]::Replace(
        $updated,
        '(?m)^Size\s+[0-9,]+\s+bytes$',
        "Size   $script:firmwareSizeText bytes",
        1)
    return $updated
}

Update-RepoFile "tools\publish-gx12-distribution.ps1" {
    param($text)
    Replace-PinnedFirmwareHash (Replace-Gx12FirmwareName $text)
}

if (Test-Path -LiteralPath (Resolve-RepoFile "tools\stage-github-repo.ps1") -PathType Leaf) {
    Update-RepoFile "tools\stage-github-repo.ps1" {
        param($text)
        Replace-Gx12FirmwareName $text
    }
}

Update-RepoFile "tests\Gx12.Launcher.Tests\Program.cs" {
    param($text)
    Replace-PinnedFirmwareHash (Replace-Gx12FirmwareName $text)
}

if ($PruneOtherRootGx12Firmware.IsPresent) {
    $firmwareRoot = Resolve-RepoFile "firmware"
    Get-ChildItem -LiteralPath $firmwareRoot -Filter "R2X-*.BIN" -File | Where-Object {
        -not [string]::Equals($_.FullName, $firmwarePathFull, [System.StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object {
        if ($PSCmdlet.ShouldProcess($_.FullName, "Remove stale root GX12 firmware file")) {
            Remove-Item -LiteralPath $_.FullName -Force
        }
        Write-Host "removed $($_.FullName)"
    }
}

Write-Host "GX12 release firmware: $firmwareFileName"
Write-Host "SHA256: $firmwareHash"
Write-Host "Size: $firmwareSizeText bytes"
