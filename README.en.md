# CoreScope

**A lightweight, always-on-top CPU and memory performance monitor for Windows.**

[简体中文](README.md) · [Downloads](https://github.com/YCTS-otree/CoreScope/releases) · [Changelog](CHANGELOG.md) · [GPL-3.0](LICENSE)

CoreScope brings per-core activity, hardware sensors and performance counters into a draggable desktop overlay. Formerly CPU Frequency Monitor, it now includes CPU and memory monitoring and experimental P-core analysis.

Current version: **1.9.4**. Built with C++17, Win32, Direct2D and DirectWrite.

## Features

- Physical/logical core topology, SMT grouping and Intel P/E core detection.
- Per-logical-processor Busy Time and its kernel portion, instantaneous frequency, physical-core/package temperature and RAPL package power.
- Actual DRAM read/write bandwidth, optional per-logical-processor IPC and P-core LLC MPKI via Windows ETW/PMC.
- Detailed and compact overlays, plus an experimental P-core TMA panel with top-level categories and Core/Memory/L1/L2/L3/DRAM Bound metrics.
- Tray controls, transparency, always-on-top, font/size/content settings, 1–10 Hz refresh, DPI awareness and scrolling.
- English and Simplified Chinese settings/tray; overlay metrics remain in English.

## Supported platforms

Intel sensors and PMU target **Alder Lake, Family 6, Models 97h/9Ah**, primarily tested on an **i7-12700KF running Windows 10 22H2 (19045)**. TMA has only been tested on that CPU's P cores; model 9Ah remains untested on hardware.

AMD Zen **Families 17h, 19h and 1Ah** have a sensor path for hardware P-state frequency, package temperature and package energy-based power. **Ryzen AI 9 HX 370 (1Ah/24h)** has been tested on Windows 11; other AMD models remain unverified. Windows topology supplies physical cores and SMT; Zen 5/Zen 5c are not labelled Intel P/E cores. AMD per-core temperature, DRAM bandwidth, IPC, LLC MPKI and TMA remain `N/A`. Compact mode uses package temperature. The bus-0 temperature interface is disabled on multi-package AMD systems.

Without PawnIO or its AMD module, frequency falls back to **Windows-reported MHz**, labelled in the overlay. This OS performance-state value may differ from instantaneous or effective clocks. Unsupported metrics remain `N/A`; other CPUs retain OS topology and PDH utilization.

Administrator privileges are required. Sensor access uses the official signed PawnIO driver. GPU, fan, SPD and per-process profiling are outside the current feature set.

## Download and run

1. Download a package from [Releases](https://github.com/YCTS-otree/CoreScope/releases), or build `CoreScope_v1.9.4_x64.zip` locally, and extract it to a writable directory.
2. If PawnIO is not installed, run the bundled installer script from an **administrator PowerShell**:

   ```powershell
   .\Dependencies\PawnIO\Install-PawnIO.ps1
   ```

   The script checks the pinned SHA-256 and Authenticode signature before running the official PawnIO 2.2.0 installer. Reinstallation is unnecessary if it is already installed.
3. Exit any old CPU Frequency Monitor / CoreScope tray instance, then launch `CoreScope.exe` and accept the elevation prompt.

Keep `IntelMSR.bin`, `IntelMCHBAR.bin`, `AMDFamily17.bin` and `tma-register.wprp` beside the executable. PawnIO is a shared system driver, potentially used by other hardware tools.

**TMA also requires a WPR version supporting extended PMU registration.** Windows Performance Toolkit WPR 10.0.28000 was tested; Windows 10's bundled 10.0.19041 failed registration. CoreScope prefers an installed WPT WPR and never installs it automatically. Registration failures degrade TMA while normal monitoring continues.

## Usage

Drag anywhere on the overlay, double-click to hide, and scroll tall windows. Left-click the tray icon for settings; right-click to select detailed, compact or advanced mode, show/hide, or exit. Closing the advanced panel returns to detailed mode.

Blue shows total utilization; dark blue is the kernel portion within that total. Refresh defaults to 4 Hz. IPC and LLC MPKI default to off; enable them in Display content. ETW resources are released when the corresponding features are disabled.

Detailed mode is the startup default. Default / Small / Very small (Laptop) presets use 250 / 230 / 180 DIP widths with smaller fonts, bars and padding. The exact legacy size default migrates automatically; existing view modes and custom layouts are preserved. Core headers and their thread bars share the spacing setting.

The Laptop preset stores width 180, gap -2, title/text 12/8, bar height 8, padding/radius 6, refresh 1 Hz, opacity 92%, and topmost enabled. View mode and display-content choices are preserved. Footer rows also follow row spacing.

Row spacing accepts signed fractional DIP values from -40 to 40, such as `-1.5` or `0.25`, and preserves them in settings. Layout retains a minimum positive row step for extreme negative values.

Settings live at `%LOCALAPPDATA%\CoreScope\settings.ini`. On first launch, the old `%LOCALAPPDATA%\CpuFreqMonitor\settings.ini` is copied if no CoreScope configuration exists. Existing CoreScope settings take precedence, and the old file is preserved. Failed copies fall back to the legacy file.

## Metric interpretation and limitations

- Busy Time uses PDH Processor Time, with Privileged Time for its kernel portion.
- Frequency uses Intel `IA32_PERF_STATUS` or AMD hardware P-state `C0010293`: FID × 5 MHz for family 1Ah, FID / DFSID × 200 MHz for 17h/19h. It is **not Effective Clock**.
- Package power uses Intel RAPL or AMD package-energy deltas divided by actual elapsed time. AMD temperature uses the SMN Tctl/Tdie register, with early Zen SKU offsets where applicable.
- RAM R/W uses actual IMC counter deltas in GB/s, without a guessed bandwidth ceiling or saturation estimate.
- IPC uses retired instructions / unhalted core cycles. LLC MPKI uses P-core LLC misses / instructions × 1000. Both update approximately once per second; E-core LLC statistics are excluded.
- TMA represents system activity across P cores, not one application. Top-level metrics use slots, while L1/L2/L3/DRAM Bound use cycles. Memory-stall attribution is not cache hit rate or a percentage breakdown within Memory Bound; these values cannot be added to slot percentages.
- Frontend Bound and Core Bound contain residual estimates. **Bad Speculation remains unvalidated**, so a displayed zero is not confirmation of no speculation waste.
- Counter groups rotate; a full TMA refresh normally takes about 10 seconds, with data age shown. Invalid or stale values become `N/A`; CoreScope does not fabricate data or automatically diagnose a bottleneck.

See [ARCHITECTURE.md](ARCHITECTURE.md) for implementation details and [TEST_RESULTS.md](TEST_RESULTS.md) for validation boundaries. These developer documents are primarily in Chinese.

## Build from source

Use Git, Visual Studio 2026 Build Tools with **MSVC v145**, and an installed Windows SDK. Projects select the installed Windows 10/11 SDK; this AMD build uses **10.0.26100.0**, while the original Intel environment uses 10.0.28000.0. TMA's WPR runtime requirement is unchanged. No Python, Node.js or package manager is needed for the application build.

From Developer PowerShell:

```powershell
git clone https://github.com/YCTS-otree/CoreScope.git
cd CoreScope
msbuild CoreScope.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
msbuild tests/hardware_tests.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild tests/pmu_tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\tests\hardware_tests.exe
.\build\tests\pmu_tests.exe
.\build\tests\hardware_tests.exe --require-sensors
.\build\tests\hardware_tests.exe --missing-module
.\build\x64\Release\CoreScope.exe --ui-test
```

The executable, modules and TMA profile are placed in `build\x64\Release\`. Runtime sensor access still requires PawnIO. Third-party binaries are pinned in `third_party/`; the build does not download dependencies or compile/sign a driver.

GUI regression and hardware tests need an interactive administrator session. Diagnostics write beside the executable and use separate test settings. `--once` shares the Intel/AMD sampler with the GUI. `--compact-live-inspect` opens a compact live overlay for about one minute. Other switches are listed by `--help`.

After building and validating, run `.\scripts\package.ps1`. It reads `shared/version.h`, checks executable version metadata and creates `Release/v<version>/` plus a ZIP with dependencies, licenses, documentation and SHA-256 checksums. Existing release output is never overwritten.

## Source layout

| Directory | Contents |
| --- | --- |
| `app/` | Win32 UI, sensors and ETW/PMC backend |
| `shared/` | Central version definition |
| `tests/` | Hardware logic and PMU regression tests |
| `scripts/` | Packaging and research tools |
| `third_party/` | Pinned upstream components, licenses and module sources |
| `Release/` | Separate versioned distribution directories |

Legacy `CpuFreqMonitor` IPC, ETW and WPR identifiers are intentionally retained to interoperate with old instances and recover abandoned sessions.

## Contributing

Report issues through [GitHub Issues](https://github.com/YCTS-otree/CoreScope/issues) or submit a pull request. Include CPU model, Windows version, CoreScope version, selected mode and error codes for hardware reports. Remove personal information from logs. New hardware profiles need independent counter/register validation and hardware testing.

## License and acknowledgements

Original CoreScope code is licensed under **GNU General Public License v3.0 only**, SPDX `GPL-3.0-only`. See [LICENSE](LICENSE). Copyright © 2026 YCTS-otree and contributors.

Third-party components retain their own licenses. Thanks to [PawnIO](https://pawnio.eu/) and [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for sources, licensing, checksums and bundled module source.
