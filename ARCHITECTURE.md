> v1.5.0 更新：RAM R/W 保留真实计数读数，删除不准确的固定 100 GB/s 标尺与比例条。
> RAM 显示关闭时暂停 IMC 读取；首次启用才初始化，重新启用先建基线。
> 浮窗固定英文；设置和托盘独立本地化。详细/精简模式共享原有采样线程和快照。
> 用户选择 1–10 Hz 控制原有 atomic interval，无新增采样线程。
> 随附官方 PawnIO 2.2.0 安装器；设备仍是系统共享的 \Device\PawnIO。

# 性能浮窗架构与硬件能力分析（1.4.0）

## 修改前的架构（1.3.0）

- `main.cpp` 负责启动参数与旧 --once CLI。GUI 由 RunMonitor 启动，频率 CLI 独立保留。
- `monitor.cpp` 包含 Win32 窗口、托盘、双语设置、DPI 布局和 Direct2D/DirectWrite 绘制。
- 单个后台 Sample 线程建立一个 PDH query，为每个 group,number 注册 Time / Utility
  和相应 Privileged counters。1.3.0 默认 Utility，与本轮 Busy Time 要求不符。
- 频率逐逻辑核绑定线程，通过 PawnIO 读取 IA32_PERF_STATUS；CPUID.16H 提供总线频率，
  未提供时使用现有现代 Intel 客户端 100 MHz 基准。
- 每逻辑核重复读取 THERM_STATUS 和 TEMPERATURE_TARGET，取最高温作为底部温度。
  没有物理核/SMT 拓扑、Package 温度、能量差分或真实内存带宽。
- Sample 把 rows 放入 mutex 保护的共享区，PostMessage 通知窗口 InvalidateRect，
  WM_PAINT 复制快照并绘制。每帧创建释放画刷，每次 MSR IOCTL 构造堆 vector。
- `PawnIoClient` 直接 NtOpenFile 打开 PawnIO 设备；加载官方签名 IntelMSR.bin；
  Execute 使用 32-byte 函数名 + uint64 参数和输出。已有业务只调用 ioctl_read_msr。

## 本轮保持和扩展

保留一个采样线程、PDH query、共享快照、窗口消息、托盘和设置体系。没有换 UI 框架、
引入驻留服务或重写驱动。新增 `hardware.h/.cpp` 集中处理静态拓扑、平台选择、只读
硬件采样、差分与颜色防抖；模型相关地址集中在 IntelRegisters，不散落到绘制逻辑。

数据流：

1. 主线程启动时：GLPIEx 建立 package/core/logical 关系；逐核亲和性 + CPUID 确认
   P/E 类型、x2APIC 和该逻辑核自身的 PMU 能力；构造 P/未知核分组、最多 6 个 E 核分组。
2. 采样线程启动时：复用 PawnIoClient，加一个 IntelMCHBAR 模块句柄；缓存 TjMax、
   RAPL energy unit、可访问 IMC bank、PMU 能力拒绝结果。
3. 约 4 Hz：PDH Busy Time；每逻辑核频率；每物理核心温度；每封装温度/能量；
   有效 IMC bank 的 read/write 计数。使用 QPC 实际时间，不假定定时器严格准时。
4. 一次锁内发布 rows + HardwareReading + 各指标颜色。窗口通知最多积压一个。
5. WM_PAINT 使用快照；画刷随渲染目标缓存；动态数组容量复用；未要求刷新时不绘制。

采样等待扣除读取耗时，不启用全局 timeBeginPeriod。正式运行不持续收集诊断历史。
测试模式才保留有上限的延迟样本。频率仍是瞬时值，不改成 Effective Clock。

## 硬件能力与来源

### 拓扑

[Microsoft PROCESSOR_RELATIONSHIP](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship)
提供物理核心 group masks，SMT sibling 来自同一 RelationProcessorCore。P/E 类型来自
CPUID.1Ah（40h=Core/P、20h=Atom/E），而非负载或频率。未知类型保留 Core 标签。
无法获得物理拓扑时明确启动失败，不把每个逻辑核冒充一个物理核心。

UI 的 Physical Core ID 是 Windows 枚举序号，hardware core ID / x2APIC ID 另列诊断，
两者不混淆。当前未为 AMD 添加传感器 profile；未来可在同一接口中新增后端。

### PawnIO / PMU 限制

[官方 IntelMSR 0.2.11 源码](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMSR.p)
只开放指定 MSR，包含 198h、19Ch、1A2h、1B1h、606h、611h；没有 309h/30Ah fixed
counters、38Dh fixed control、38Fh global PMU control 或通用事件选择寄存器。
实机读取 309h 返回 Win32 ERROR_ACCESS_DENIED (5)。

因此本轮 **IPC、LLC Miss、LLC Reference、LLC MPKI 不可用**。只是检测到 CPUID
fixed counter 数量不能证明它们正在正确计数，更不能假定没有被 Windows/Profiler 使用。
没有做 RDPMC 权限绕过，也不尝试把 APERF 当作 retired instructions。

后续 PMU 后端需要可信签名模块提供：批量读取、明确的计数器所有权/保存恢复、启用状态
和计数范围确认、相同窗口的 retired instructions / active cycles。LLC 事件必须按
family/model + P/E 类型查事件表；P/E 的可编程事件、限制和聚合范围不能混用。
MPKI 应用该窗口有效 misses / 同范围 retired instructions * 1000，任一缺失则不可用。
这不是当前平台不具备 PMU，而是当前可用签名访问链路不提供所需能力。

### RAPL / 温度

读取 Intel SDM 的 RAPL_POWER_UNIT 和 PKG_ENERGY_STATUS，32-bit 模差乘 energy unit
再除以 QPC 秒数。首样本或长暂停不显示功率。Package 温度来自专用状态寄存器，
不是最高核心温度。每物理核心只读一份温度、启动缓存 TjMax。只看当前 throttling bit，
不把历史 log bit 当作持续节流。

### IMC

[官方 IntelMCHBAR 0.2.11](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMCHBAR.p)
提供受范围/对齐检查的只读 qword 接口。采用
[Intel PCM bw.cpp](https://github.com/intel/pcm/blob/master/src/bw.cpp) 中 TGLClientBW
读取方式；[cpucounters.cpp](https://github.com/intel/pcm/blob/master/src/cpucounters.cpp)
显式为 ADL 选择这一方式，而不是旧的 ADLClientBW（上游注明后者另需 2x 系数）。

读取偏移按两套 bank（5058h/50A0h、D858h/D8A0h）及 10000h 步长组合；本机前一套
在两个控制器上均返回全 1，后一套有效。启动确认读写成对可用，之后不读取未实现 bank。
仅一个方向无效时整体 unavailable，避免把部分数据当成总量。

[PCM 字节换算](https://github.com/intel/pcm/blob/master/src/cpucounters.h) 为事务增量 * 64。
低 32-bit 模差遵循上游扩展计数方式，超过 2 秒间隔不报告速率，避免多次回绕风险。
只读、无需写 PMU/IMC 配置。瞬态访问失败后停止相关不可用源的重复轮询，重启可重新探测。

## 验证边界

硬件读数和有限时长开销已实测，不能将一次测试当作所有主板/BIOS 的认证。
IMC 对照为大于 LLC 的 128 MiB 内存复制缓冲区；包括系统其余内存流量，不能拿 memcpy
用户字节数与 DRAM 事务一比一等同（写分配、缓存及其他进程都会影响）。
未增加 SPD，也没有自动判定 CPU/内存瓶颈。其他架构的硬件传感器需新增并验证 profile。
