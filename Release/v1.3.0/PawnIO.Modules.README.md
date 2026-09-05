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
