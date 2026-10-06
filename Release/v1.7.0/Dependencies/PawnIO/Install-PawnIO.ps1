#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$Reinstall)
$ErrorActionPreference = 'Stop'
$setupPath = Join-Path $PSScriptRoot 'v2.2.0/PawnIO_setup.exe'
$expectedHash = '1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032'
if ((Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'PawnIO installer hash mismatch.' }
$signature = Get-AuthenticodeSignature -LiteralPath $setupPath
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=namazso.eu(?:,|$)') { throw 'PawnIO installer signature verification failed.' }
if ($Reinstall) {
    $uninstall = Start-Process -FilePath $setupPath -ArgumentList '-uninstall','-silent' -WindowStyle Hidden -PassThru -Wait
    if ($uninstall.ExitCode -ne 0) { throw "PawnIO uninstall failed: $($uninstall.ExitCode)" }
}
$install = Start-Process -FilePath $setupPath -ArgumentList '-install','-silent' -WindowStyle Hidden -PassThru -Wait
if ($install.ExitCode -eq 183) { Write-Output 'PawnIO is already installed. To replace it using this bundled installer, close hardware monitors and run with -Reinstall.'; exit 183 }
if ($install.ExitCode -ne 0) { throw "PawnIO install failed: $($install.ExitCode)" }
Write-Output 'Bundled PawnIO 2.2.0 installed successfully. Restart the monitor.'
