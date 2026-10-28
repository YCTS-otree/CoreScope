> v1.6.2 更新：接入按需启动的 ETW IPC / P 核 LLC MPKI；RAM R/W 保留真实计数读数，删除不准确的固定 100 GB/s 标尺与比例条。
> RAM 显示关闭时暂停 IMC 读取；首次启用才初始化，重新启用先建基线。
> 浮窗固定英文；设置和托盘独立本地化。详细/精简模式共享原有采样线程和快照。
> 用户选择 1–10 Hz 控制原有 atomic interval，无新增采样线程。
> 随附官方 PawnIO 2.2.0 安装器；设备仍是系统共享的 \Device\PawnIO。

# CoreScope 架构与硬件能力分析

当前版本 1.9.1；以下按版本记录采集架构的演进。1.8.1 统一产品名称并迁移配置路径；1.9.0 在既有采样器内新增 AMD 路径，保留 Intel 后端和内部会话标识。

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
两者不混淆。AMD 传感器路径从 1.9.0 起在同一接口内提供，见文末。

### ETW / PMU 后端（v1.6.2）

`PmuSampler` 与 PawnIO 独立；在采样线程管理一个命名的 Windows system logger，专用线程通过 ProcessTrace 消费实时 CSwitch PMC 扩展数据。温度/频率/带宽不依赖 PMU 成败。仅已验证的 Family 6 Model 97h/9Ah 和 48-bit fixed counters 启用。

仅 IPC/LLC 显示开启时申请固定指令、核心周期计数器；LLC 开启时增加 LLCMisses。直接 TraceSetInformation 提交 source 列表，按该列表解析扩展数据；较新系统会话查询或配置元数据用于交叉检查。Windows 10 19045 实测不支持 TracePmcSessionInformation，使用直接 API 提交顺序，与 WPR 重建/排序配置列表不同。

全局 owner mutex 防止多个实例竞争本程序的会话。取得独占 owner 后只清理同名且 GUID 一致的遗留会话；不停止其他工具的 logger，不接管已有 PMU 资源。初始化任一步失败均停止本次会话、关闭 consumer、join 线程、释放 owner；关闭显示、模式切换和正常退出使用相同清理路径。异常退出留下的本程序会话在下一次启动恢复。

每逻辑处理器按 Windows ProcessorIndex 映射到 group/number，在事件时间（QPC）相邻计数器上取差值。下降/可能回绕、超过 2 秒的中断、零周期、异常数量均重新建立基线。每约 1 秒同时发布各核累计窗口；相同数据最多显示 2 秒，之后 N/A。MPKI 按当前发布窗口内 P 核 misses / retired instructions × 1000，任一 P 核没有有效数据时 N/A；E 核 IPC 可用，E 核 LLC 口径未校准，因此 UI 明确显示 `LLC MPKI (P)`。

每秒查询自身会话丢失统计；消费线程退出、事件丢失、配置错误或启动 5 秒无数据时使数据失效并清理，以 30 秒退避重试。关闭/重开显示项可立即重新探测。管理员进程只启用自身 SeSystemProfilePrivilege，不更改系统权限/安全设置。正常模式不写 ETL 文件；诊断构造参数可为测试保存文件。

统计是核心活动窗口，包含系统/其他线程/中断，不是某个应用的独占 IPC；CSwitch 采样边界和缓冲交付存在延迟。实时流高频时存在额外 CPU/ETW 缓冲开销。已验证的窗口/生命周期测试不等同于 E 核 LLC 指标准确度认证。

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
PawnIO 传感器只读；PMU 由 Windows ETW 管理。瞬态访问失败后停止相关不可用源的重复轮询，重启可重新探测。

## 验证边界

硬件读数和有限时长开销已实测，不能将一次测试当作所有主板/BIOS 的认证。
IMC 对照为大于 LLC 的 128 MiB 内存复制缓冲区；包括系统其余内存流量，不能拿 memcpy
用户字节数与 DRAM 事务一比一等同（写分配、缓存及其他进程都会影响）。
未增加 SPD，也没有自动判定 CPU/内存瓶颈。其他架构的硬件传感器需新增并验证 profile。

## v1.7.0 TMA / UI

PmuSampler 使用 canonical 8 个位置：固定 instructions/cycles、可选 LLC、P 核 slots/retired/badspec/backend/memory。按 active mask 提交紧凑源列表，配置元数据或会话查询映射回 canonical；旧系统直接 API 使用提交顺序。每 1 秒仅汇总所有有效 P LP 的 5 个槽位计数；E LP 不参与 TMA。

事件注册通过安装的 WPT WPR（无 WPT 时系统 WPR）读取随附 profile，以独立 `CpuFreqMonitor.TMA.Register.v1` 命名实例短时注册/取消；只有取得 product owner mutex 后才调用，不使用全局 cancel。每个子进程等待限时 5 秒，注册失败回退原来的 IPC/LLC 会话。运行期间自定义描述非持久注册，重启清除。后端继续统一丢失/退出/暂停/基线检查。

TmaReading 暴露原始比值，Bad Speculation 明确未验证；Frontend/Core 的负余数显示 N/A。Memory Bound 实验性；L1/L2/L3/DRAM 没有采集，UI 保留 N/A 行。独立 GDI 浮窗按实际文字和字体测量宽度与行高，与主窗 DPI/置顶/透明度设置衔接，位置独立。进阶模式序列化为 ViewMode=2，兼容旧 Detailed 键。

精简模式循环物理核心，将标签与第一份有效 sibling 频率垂直居中于原来的整颗核心行组，P 核条组采用设置的精简条高加 5 个实际像素，并限制在已有核心行组内，SMT 两条贴合且居中，保留各线程的 IPC。大字体 TMA 面板可滚轮访问超出屏幕的行。很小预设精确采用 250 DIP / 9 DIP / 6 DIP / 1 Hz 等用户参数。


## v1.8.0 Memory Bound

canonical 扩展到 12 个位置，增加 load-bound、L1/L2/L3 miss stall cycles。单会话两组每 5 秒轮换：槽位组延续顶层 TMA；周期组固定 instructions/cycles，并提交 Slots、BoundLoads、LLC（关闭 LLC 显示时用 Retired 填充）、三个受限 stall 事件。两组均保留固定 IPC 计数；LLC 启用时均采集 MPKI。

本机 HAL 的计数器分配对 restricted events 至关重要：MEMORY_ACTIVITY 只允许 PMC0–3。实测少量源或排在前面时 L1/L2 为零；填满六个 generic 位置并把三个 stall 源最后提交后四项有效。本轮仅在 Windows 10 19045 / i7-12700KF 验证，不能将这一排列认作 Windows 跨版本分配保证。诊断配置和负载证据见 scripts/research/MEMORY_BOUND_RESULTS.md。

轮换先停止自有 session、关闭 consumer 并 join，然后建立新基线，绝不跨计数器组做差；轮换全程保持 product owner 锁，避免其他实例在轮换间隙取得本程序的会话。每 1 秒将同组所有有效 P LP 增量按周期加权汇总，不纳入 E LP。两个 TMA 缓存独立记时，12 秒过期；只有轮换保留缓存，关闭、配置变化和错误全部清空。IPC/MPKI 在重建基线时保留不足 2 秒的最近有效值，过期显示 N/A。轮换配置失败退回普通 IPC/LLC，并按原有 30 秒退避重试 TMA。

Memory Bound 分区使用 Intel 周期公式，而顶层/Backend 分区使用 slots。无周期、计数异常超界、负的层级差分别失效，不强行归一化。L1 的 max(...,0) 是 Intel 原公式；其余负数显示 N/A。数据年龄和轮换间隔显式显示，避免误解为四项与顶层同一时刻。

## AMD Zen 传感器（v1.9.0）

- `DetectTopology` 分别识别 GenuineIntel / AuthenticAMD；按 CPUID 的基础 family 判断扩展 model 位，修复 17h/19h/1Ah 型号误判。AMD 使用 8000001Eh 的扩展 APIC/core ID，物理核心与 SMT 仍由 Windows GLPIEx 决定，Zen 5/Zen 5c 保留 Core 标签。
- 官方签名 `AMDFamily17.bin` 固定为项目已有 PawnIO.Modules 0.2.11，随源代码、构建输出和发布包一起分发；未编译或签署新模块。只调用 MSR/SMN 读取入口，不修改 P-state、CPPC、功率限制或 PMU 控制寄存器。
- Family 17h/19h 的 `C0010293` 使用 FID / DFSID × 200 MHz；1Ah 使用低 12 位 FID × 5 MHz。零除数、零频率及超出有效范围的读数无效。这是硬件 P-state 频率，不是 APERF 有效频率；CLI `--once` 与 GUI 共用后端，同时保留旧 Intel CLI 的频率支持范围。
- `C0010299[12:8]` 定义能量单位，`C001029B` 低 32 位封装能量按 QPC 采样间隔取模差。首样本、长暂停或读取失败不显示功耗，各封装分别建立基线；任何封装缺失时不显示部分总量。
- 单封装机器通过 SMN `59800h` 读取封装 Tctl/Tdie，按 RANGE_SEL/TJ_SEL 处理 49°C 调整，并处理早期 Ryzen / Threadripper 的型号温差。每次 SMN 索引/数据事务持有官方模块要求的共享 `Global\Access_PCI` mutex；超时跳过本次温度，不阻塞整个采样周期。模块仅访问 bus 0，因此多封装 AMD 不启用该温度接口。HX 370 的 100°C Tjmax 来自 AMD 官方规格，其他 SKU 不猜测告警阈值。
- 模块或驱动不可用时，AMD 频率使用 PDH `Processor Information(group,number)\Processor Frequency`，界面明确标为 Windows reported；它可能是系统性能状态值，不可当作有效频率。仅在需要回退时收集该查询，查询随采样器释放。
- 不将封装温度复制为逐核温度。详细模式逐核温度保持 N/A，精简模式显示封装温度。AMD RAM R/W、IPC、LLC MPKI、TMA 保持不支持，不加载 IntelMCHBAR，也不启动 Intel PMU/TMA 会话。

核对来源：
- [PawnIO.Modules 0.2.11 AMDFamily17](https://github.com/namazso/PawnIO.Modules/blob/0.2.11/AMDFamily17.p)
- [LibreHardwareMonitor AMD 实现](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Cpu/Amd17Cpu.cs)
- [Linux k10temp 温度转换](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c)
- [AMD HX 370 规格](https://www.amd.com/zh-cn/support/downloads/drivers.html/processors/ryzen/ryzen-ai-300-series/amd-ryzen-ai-9-hx-370.html)

仅 HX 370 完成本轮实机验证；其他 Zen 型号和多封装硬件尚未验证。
