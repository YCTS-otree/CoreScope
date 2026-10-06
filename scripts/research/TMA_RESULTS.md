# TMA acquisition validation — 2026-10-06

Scope: diagnostics only; no product integration, advanced window, version bump or release.
Host: i7-12700KF, Windows 10 19045; P-core LP0–15, E-core LP16–19.

## Result
Partial acquisition succeeds, but complete trustworthy TMA is NOT validated.
The installed Windows SDK WPR 10.0.28000 can register nonpersistent custom Intel PMU sources and record four counters on CSwitch. Architecture must be INTEL (confirmed by installed WPRControlProfiles.xsd), not X64. Initial X64 attempt did not register events; it must not be interpreted as hardware failure.

Successful diagnostic trace: build/tma-order-probe.etl. Analyzer output: build/tma-order-analysis.txt.
982347 PMC records; ProcessTrace=0; EventsLost=0; malformed=0; no decreasing-counter resets.
The trace explicitly supplies counter-name mapping.
P-core slots, retired uops and backend slots are nonzero and respond to workloads.
TOPDOWN.BAD_SPEC_SLOTS is zero on every P logical processor, including an unpredictable conditional-branch stress phase. Renaming it to sort first in WPR's recorded counter list did not resolve the issue. Recorded array order does NOT establish physical PMC placement.
Intel documents this event as PMC0-only. Incorrect HAL placement is a hypothesis, not a proven cause.
Because bad speculation is unvalidated, all four-way percentages (including frontend computed as remainder) in diagnostic output are provisional and MUST NOT be displayed as valid TMA.
E-core slots/backend/badspec are zero with this P-core encoding set. This does not prove E-core TMA impossible; E cores need their own documented events/formulas and separate validation.

## Resource handling
The probe first confirmed WPR idle, created only its own recording, and stopped it successfully; final status is WPR is not recording. The user's running application was not stopped or modified.
Persist=false avoids persistent registry registration. Custom source names remain enumerated after stop on this boot; this is not an active recording and we have not claimed their removal.
No new driver, SDK, or toolchain was installed.

## Sources
- https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-pmu-events
- https://perfmon-events.intel.com/platforms/alderlake/core-events/p-core/

Next verification, if continued: validate PMC0 placement or use a documented alternate event/formula set, demonstrate bad-speculation sensitivity, independently validate E-core metrics and coexistence with the product's IPC/LLC counters. Product readiness remains unproven.
