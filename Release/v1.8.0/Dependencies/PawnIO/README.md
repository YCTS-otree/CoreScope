# Bundled PawnIO 2.2.0

Official signed installer downloaded 2026-09-07 from:
https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe

Latest release at verification: 2.2.0 (2026-03-15).
Official website: https://pawnio.eu/
SHA256: 1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032
Authenticode: Valid; signer namazso.eu.
The installer is distributed unchanged. Its embedded license/terms apply.

From an administrator PowerShell, run `./Install-PawnIO.ps1`.
To intentionally replace the installed driver using this bundled package, close
hardware monitors and run `./Install-PawnIO.ps1 -Reinstall`.
The script validates the pinned hash and Authenticode before execution.
It never enables unrestricted mode or changes Windows security settings.

PawnIO is a system-wide kernel driver at the shared device `\Device\PawnIO`.
Bundling the installer makes deployment reproducible; it does not create a
project-private driver instance. Other applications using PawnIO share it.

On this development machine, replacement with this exact package was completed:
official uninstall exit 0, official install exit 0; PawnIO driver Running.
