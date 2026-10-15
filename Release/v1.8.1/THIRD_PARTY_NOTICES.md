# Third-party notices

CoreScope original source is GPL-3.0-only. This does not change the licenses of the independent third-party components below, including copies in historical `Release/` directories.

## PawnIO.Modules 0.2.11

- Copyright: namazso and the upstream contributors; upstream notices are preserved in the source archive.
- License: **LGPL-2.1-or-later**. Full text: [`third_party/PawnIO.Modules/COPYING`](https://github.com/YCTS-otree/CoreScope/blob/v1.8.1/third_party/PawnIO.Modules/COPYING).
- Project: https://github.com/namazso/PawnIO.Modules
- Pinned source: https://github.com/namazso/PawnIO.Modules/tree/0.2.11
- Complete upstream source archive: [`third_party/PawnIO.Modules/source-0.2.11.tar.gz`](https://github.com/YCTS-otree/CoreScope/blob/v1.8.1/third_party/PawnIO.Modules/source-0.2.11.tar.gz). New CoreScope packages include the same archive under `Sources/` and the license as `COPYING.PawnIO.Modules`.
- Official binary release: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
- `IntelMSR.bin` SHA-256: `D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F`
- `IntelMCHBAR.bin` SHA-256: `3F82B832D99B4AAC37D2A20FDB7C9BAA2A3BC0488612C9019C9484EB0E8A6EAE`

The application loads the unmodified, officially signed modules through the PawnIO runtime. It does not statically link the module code or compile/sign replacement modules. See upstream source for its build instructions and dependencies. Module files remain replaceable, subject to the official driver's signature validation.

## Official PawnIO 2.2.0 installer

- Author/publisher: namazso / namazso.eu.
- Project and runtime information: https://pawnio.eu/
- Official distribution: https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0
- Pinned unmodified installer: `third_party/PawnIO.Setup/v2.2.0/PawnIO_setup.exe`.
- SHA-256: `1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032`
- The official installer and signed runtime are separate upstream software; their included license and installation terms apply. They are **not relicensed under CoreScope's GPL-3.0 license**.
- Upstream driver source and licensing: https://github.com/namazso/PawnIO

The bundled CoreScope installation script validates the pinned digest and Authenticode publisher before execution. It does not enable unrestricted mode, install an unsigned driver, or change Windows security settings. PawnIO is a shared system driver.

## Windows platform components

CoreScope uses Windows APIs, PDH, ETW/PMC, Direct2D and DirectWrite from the installed operating system. Visual Studio, the Windows SDK and optional Windows Performance Toolkit are external development/runtime prerequisites and are not included in the CoreScope distribution.
