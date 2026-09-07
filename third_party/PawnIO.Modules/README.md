# PawnIO Intel MSR module

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
