# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$versionHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'shared/version.h') -Raw
$versionMatch = [regex]::Match($versionHeader, '#define\s+CORESCOPE_VERSION_STRING\s+"(\d+\.\d+\.\d+)"')
if (-not $versionMatch.Success) { throw 'Cannot read CoreScope version.' }
$version = $versionMatch.Groups[1].Value
$executable = Join-Path $projectRoot 'build/x64/Release/CoreScope.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build Release x64 before packaging.' }
$metadata = (Get-Item -LiteralPath $executable).VersionInfo
if ($metadata.ProductName -ne 'CoreScope' -or $metadata.ProductVersion -ne $version) {
    throw 'Executable metadata does not match shared/version.h; rebuild before packaging.'
}
$releaseRoot = Join-Path $projectRoot 'Release'
$destination = Join-Path $releaseRoot "v$version"
$archive = Join-Path $releaseRoot "CoreScope_v${version}_x64.zip"
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $archive)) {
    throw "Release v$version already exists; previous releases must not be overwritten."
}
$required = @(
    'README.md', 'README.en.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
    'CHANGELOG.md', 'ARCHITECTURE.md', 'TEST_RESULTS.md', 'app/tma-register.wprp',
    'third_party/PawnIO.Modules/IntelMSR.bin', 'third_party/PawnIO.Modules/IntelMCHBAR.bin',
    'third_party/PawnIO.Modules/AMDFamily17.bin',
    'third_party/PawnIO.Modules/COPYING', 'third_party/PawnIO.Modules/README.md',
    'third_party/PawnIO.Modules/source-0.2.11.tar.gz',
    'third_party/PawnIO.Setup/README.md', 'third_party/PawnIO.Setup/Install-PawnIO.ps1',
    'third_party/PawnIO.Setup/v2.2.0/PawnIO_setup.exe'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relative) -PathType Leaf)) {
        throw "Missing required package input: $relative"
    }
}
$pinned = @{
    'third_party/PawnIO.Modules/AMDFamily17.bin' = 'DAE74615761B78BDF064DFB3E136252DDCC6FC727D88F14738D0E5800D427A91'
    'third_party/PawnIO.Modules/IntelMSR.bin' = 'D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F'
    'third_party/PawnIO.Modules/IntelMCHBAR.bin' = '3F82B832D99B4AAC37D2A20FDB7C9BAA2A3BC0488612C9019C9484EB0E8A6EAE'
    'third_party/PawnIO.Setup/v2.2.0/PawnIO_setup.exe' = '1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032'
}
foreach ($relative in $pinned.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $relative) -Algorithm SHA256).Hash -ne $pinned[$relative]) {
        throw "Pinned dependency hash mismatch: $relative"
    }
}
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath $executable -Destination $destination
foreach ($name in @('README.md','README.en.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','ARCHITECTURE.md','TEST_RESULTS.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $destination
}
foreach ($name in @('IntelMSR.bin','IntelMCHBAR.bin','AMDFamily17.bin')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party/PawnIO.Modules/$name") -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'app/tma-register.wprp') -Destination $destination
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/PawnIO.Modules/COPYING') -Destination (Join-Path $destination 'COPYING.PawnIO.Modules')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/PawnIO.Modules/README.md') -Destination (Join-Path $destination 'PawnIO.Modules.README.md')
# Resolve repository-relative third-party links for the standalone distribution.
$noticePath = Join-Path $destination 'THIRD_PARTY_NOTICES.md'
$noticeText = [System.IO.File]::ReadAllText($noticePath)
$noticeText = $noticeText.Replace('](third_party/', '](https://github.com/YCTS-otree/CoreScope/blob/v' + $version + '/third_party/')
[System.IO.File]::WriteAllText($noticePath, $noticeText, [System.Text.UTF8Encoding]::new($false))
$moduleNoticePath = Join-Path $destination 'PawnIO.Modules.README.md'
$moduleNotice = [System.IO.File]::ReadAllText($moduleNoticePath).Replace('`source-0.2.11.tar.gz`', '`Sources/PawnIO.Modules-0.2.11.tar.gz`').Replace('`COPYING`', '`COPYING.PawnIO.Modules`')
[System.IO.File]::WriteAllText($moduleNoticePath, $moduleNotice, [System.Text.UTF8Encoding]::new($false))
$sourceDirectory = Join-Path $destination 'Sources'
New-Item -ItemType Directory -Path $sourceDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/PawnIO.Modules/source-0.2.11.tar.gz') -Destination (Join-Path $sourceDirectory 'PawnIO.Modules-0.2.11.tar.gz')
$dependencyDirectory = Join-Path $destination 'Dependencies/PawnIO'
New-Item -ItemType Directory -Path $dependencyDirectory -Force | Out-Null
foreach ($name in @('README.md','Install-PawnIO.ps1','v2.2.0')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party/PawnIO.Setup/$name") -Destination $dependencyDirectory -Recurse
}
$checksums = Get-ChildItem -LiteralPath $destination -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($destination.Length + 1).Replace('\','/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
$checksums | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt') -Encoding ascii
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $archive -CompressionLevel Optimal
$archiveChecksum = '{0}  {1}' -f (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path -Leaf $archive)
$archiveChecksum | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Output "Packaged CoreScope $version to $destination"
Write-Output $archiveChecksum
