# PawnIO sensor modules

- Upstream: https://github.com/namazso/PawnIO.Modules
- Release: 0.2.11
- Asset: `release_0_2_11.zip`
- Module: `IntelMSR.bin`
- SHA-256: `D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F`
- License: GNU LGPL 2.1 or later; see `COPYING`.

The official signed PawnIO driver validates this module before loading it. The
module allowlists `IA32_PERF_STATUS` (`0x198`) and executes the read on the
logical processor selected by the calling thread's group affinity.

## IntelMCHBAR module (added in 1.4.0)

- Same official release: 0.2.11 / release_0_2_11.zip
- Module: `IntelMCHBAR.bin`
- SHA-256: `3F82B832D99B4AAC37D2A20FDB7C9BAA2A3BC0488612C9019C9484EB0E8A6EAE`
- Source: https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMCHBAR.p
- License: GNU LGPL 2.1 or later (same COPYING).
- The app loads the official signed module and uses only ioctl_read_qword for verified Alder Lake IMC counters.
- Module files were extracted from the upstream release; no local module compilation or signing is performed.

## AMDFamily17 module (added in 1.9.0)

- Same pinned official release: 0.2.11 / release_0_2_11.zip.
- Module: `AMDFamily17.bin`.
- SHA-256: `DAE74615761B78BDF064DFB3E136252DDCC6FC727D88F14738D0E5800D427A91`.
- Source: https://github.com/namazso/PawnIO.Modules/blob/0.2.11/AMDFamily17.p
- License: LGPL-2.1-or-later (same `COPYING` and complete source archive).
- CoreScope only calls `ioctl_read_msr` and `ioctl_read_smn`; it never invokes the module's MSR write interface. SMN index/data transactions hold the required shared `Global\Access_PCI` mutex.
- Official release archive SHA-256: `43608CB89BC84247FEF1368A139013F7D043E17DB6D6C8DFC9B46BF0905A81F4`.

## Corresponding source

The complete unmodified upstream 0.2.11 source is included in `source-0.2.11.tar.gz`.
Source URL: https://codeload.github.com/namazso/PawnIO.Modules/tar.gz/refs/tags/0.2.11
The original source notices and LGPL-2.1-or-later license remain applicable.

Source archive SHA-256: `AFE6EC8B43189376107D860D9BA875A0D481DBC2A5A5E965D6232D6CE267CC4A`
