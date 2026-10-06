# ETW PMU 验证 — 2026-10-06

结论：本机 Windows 自带 ETW/PMC 访问路径可以实际读取指令、核心周期与 LLC miss。无需新增驱动、签名申请、PawnIO 模块修改或测试模式。本次仅增加独立诊断工具；监控程序仍为 v1.5.0，IPC/LLC MPKI 显示未修改。

## 环境和采集

- Windows 10，10.0.19045；本机 20 个逻辑处理器，LP0–15 为 P 核，LP16–19 为 E 核（按每核亲和性下的 CPUID.1AH 确认）。
- 使用系统 `C:\Windows\System32\wpr.exe`；已安装的 Windows 8.1 xperf 不支持 `-pmcsources`，因此没有用于录制。
- 普通权限启动返回 `0x80070005`；经过用户同意，通过 UAC 以管理员权限启动后成功。
- `wpr -pmcsources` 枚举出 InstructionsRetiredFixed、UnhaltedCoreCyclesFixed、LLCMisses 等事件。
- WPR 自定义 profile 使用 `Strict="true"`，在 CSwitch 上携带三个硬件计数器，不能静默忽略配置失败。
- 首次录制：1,418,755 条携带 PMC 的事件；第二次对照：855,430 条。两次 ETL 均记录 EventsLost=0，20 个逻辑处理器均有三个计数器的有效非零差值。
- 诊断使用现有 MSVC 编译用户态程序，仅调用 Windows ETW 读取 API；未构建内核驱动。

## 数据解释和负载对照

ETL 的 PmcCounterConfig_V2 明确记录实际排列为 `InstructionsRetiredFixed, LLCMisses, UnhaltedCoreCyclesFixed`，与 WPRP 的声明顺序不同。分析器按记录中的名称匹配计数器，不能直接按声明顺序计算。

对同一逻辑处理器相邻 CSwitch 事件的单调差值进行计算：IPC = instructions / unhalted core cycles；MPKI = LLCMisses * 1000 / instructions。每核 CPUID.0AH.EBX 返回 0，未报告所定义的架构事件不可用。

每个负载阶段约 3 秒；计算负载为寄存器依赖整数循环，内存负载为 256 MiB 缓冲区的大跨度依赖读取。阶段使用精确系统时间标记，分析时排除跨阶段边界的差值。

| 核心 / 阶段 | IPC | 原始 LLC MPKI |
|---|---:|---:|
| LP0 P 核，计算 | 0.9974 | 0.0208 |
| LP0 P 核，内存 | 0.0177 | 126.4873 |
| LP16 E 核，计算 | 0.6658 | 0.0260 |
| LP16 E 核，内存 | 0.0132 | 914.5148 |

这证明所需计数数据可以取得，并对负载变化作出响应；不是正式产品指标精度校准。统计范围是该逻辑处理器的活动，包括期间其他线程、系统工作和中断，并非仅测试进程。

尤其 E 核内存阶段 LLC miss / instructions 比例很高，尚未解释全部差异，不能直接将 P/E 原始 MPKI 混合成已验证的整机指标。Intel 的 Alder Lake E 核 LONGEST_LAT_CACHE.MISS 描述包含 demand load、RFO、取指和 L1 硬件预取，且属于 speculative 事件；并非单纯的 retired load miss。正式接入需要确认 Windows HAL 的事件映射与统计范围，分别验证 P/E 核口径，以及实时处理开销、资源冲突、计数回绕和会话生命周期。

## 复现和输出

1. 在项目根目录执行 `scripts\research\build-etw-pmu.cmd`，使用现有 VS Build Tools 编译。
2. 管理员 PowerShell 执行 `scripts\research\run-etw-pmu.ps1 -OutputStem <新的简单名称>`，录制前会确认 WPR 当前未录制。
3. ETL、负载时间标记、分析和录制日志保存到被 Git 忽略的 `build/`。`*-complete.txt` 为 passed 表示读取通过，不代表正式指标精度认证。

本次证据：`build/etw-pmu-validation.etl`、`build/etw-pmu-validation-load.txt`、`build/etw-pmu-validation-analysis.txt`、`build/etw-pmu-validation-run.txt`。
两次录制结束后 WPR 均确认 `WPR is not recording`。分析源码修正后的结果以 analysis.txt 为准。

参考：
- [Microsoft：Recording Hardware Performance (PMU) Events](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-pmu-events)
- [Intel：Alder Lake E-Core Events](https://perfmon-events.intel.com/platforms/alderlake/core-events/e-core/)
