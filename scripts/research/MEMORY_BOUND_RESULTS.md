# Memory Bound verification — 2026-10-13

Host: i7-12700KF (model 97h), Windows 10 19045; existing WPT WPR 10.0.28000, no new driver or installation.

The four Intel P-core events are BOUND_ON_LOADS (A6/21, CMask 5), STALLS_L1D_MISS (47/03, CMask 3), STALLS_L2_MISS (47/05, CMask 5), STALLS_L3_MISS (47/09, CMask 9). The latter three require PMC0–3. WPR ExtendedBits accepts eight digits without a 0x prefix; the prefixed probe was rejected (c558063a).

Initial seven-source collection (two fixed plus five generic) returned L1/L2 stalls zero and invalid negative L3 differences. L1 alone was also zero. A nine-source group was rejected as busy (800700aa). The successful eight-source group uses two fixed plus six generic, with three unrestricted events first and three restricted stall events last. This is an observed HAL placement workaround, not a documented portability guarantee. Native API product collection with the same arrangement also returns nonzero valid ratios with LLC enabled and disabled.

The diagnostic tool pins a dependent random pointer-chase to P LP0 and records four warmed-up 4-second phases. Trace v5 has explicit counter-name mapping. All figures below are cycles percentages of that LP in the same phase (not slots or cache hit rates).

| Working set | Load bound | L1 stalls | L2 stalls | L3 stalls |
| --- | ---: | ---: | ---: | ---: |
| 16 KiB | 75.919 | 0.617 | 0.587 | 0.476 |
| 512 KiB | 92.197 | 35.793 | 0.943 | 0.663 |
| 8 MiB | 99.016 | 96.024 | 94.670 | 83.663 |
| 128 MiB | 98.798 | 90.107 | 87.683 | 77.231 |

These demonstrate nonzero event response and the expected change in lower-cache stalls. Larger working sets are not pure isolated L3/DRAM workloads; shared background activity and actual cache allocation affect attribution. This is not accuracy certification. Evidence: build/memory-bound-probe-v5.etl, -analysis.txt and -load.txt; build/memory-native-test.txt (initial native integration lifecycle exit 0).

Intel formulas: L1=max(loadBound−stallL1,0)/cycles; L2=(stallL1−stallL2)/cycles; L3=(stallL2−stallL3)/cycles; DRAM=stallL3/cycles, multiplied by 100. Product uses a cycle-weighted aggregate of all P LPs and rejects stale or inconsistent data. E cores are excluded.

Sources:
- https://perfmon-events.intel.com/platforms/alderlake/core-events/p-core/
- https://raw.githubusercontent.com/intel/perfmon/main/ADL/metrics/alderlake_metrics_goldencove_core.json
- https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-pmu-events

The existing Bad Speculation zero-count limitation remains separate and explicitly unvalidated at the user's request.
