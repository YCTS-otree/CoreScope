# 1.1.1 测试结果

测试环境：Windows 10 Pro 22H2 Build 19045，Intel Core i7-12700KF，20 个逻辑
处理器。

- PawnIO 服务：2.2.0，运行中。
- PawnIO.sys：Windows 报告数字签名有效；签名者为 Microsoft Windows Hardware
  Compatibility Publisher。
- IntelMSR.bin：PawnIO.Modules 官方 0.2.11 发布包。
- Release x64：Visual C++ `/W4 /WX` 构建通过，0 个警告、0 个错误。
- 未提升权限运行：按 PawnIO 安全策略返回 `ERROR_ACCESS_DENIED`，符合预期。
- 管理员权限实机频率读取：通过。20 个逻辑处理器全部成功读取；CPU00–CPU15
  为 5000 MHz，CPU16–CPU19 为 4000 MHz，与测试机的 P/E 核设定一致。
- 对照 LiteMonitor：其 CPU 硬件后端同样采用 LibreHardwareMonitor + PawnIO；
  本项目省略完整硬件库，直接使用 PawnIO Intel MSR 模块读取逐线程倍频。

# 1.2.0 测试结果（2026-09-06）

- MSVC v145 Release x64 `/W4 /WX` 编译链接通过。
- `--ui-test`：退出码 0；验证模拟刷新、最大条目字号与窄宽度组合的自动扩宽、
  无效透明度回退、双击隐藏、设置窗口关闭和线程退出。
- `--sensor-test`：退出码 0；20 个逻辑处理器均获得总占用与内核占用读数，
  内核占用不大于总占用。Core0–15 为 5.000 GHz，Core16–19 为 4.000 GHz，
  当次最高有效核心温度 62.0°C。
- 单实例：连续启动三次，后两次退出码均为 0，最终只剩一个进程（PID 24012）。
  后续启动通过注册窗口消息请求已有实例显示浮窗。
- 用户实测反馈已确认托盘图标可见、浮窗能够拖动。用户发现的多实例问题已修复。
- 视觉检查限制：computer-use 可枚举演示窗口，但窗口截图连续两次失败：
  `SetIsBorderRequired failed: 不支持此接口 (0x80004002)`。
  因此未宣称完成截图验收；实际字体渲染、跨显示器 DPI 切换仍需进一步人工检查。
- 自检使用独立窗口类，不创建托盘图标，不读取/保存用户设置，也不占用正常实例锁。

# 1.3.0 测试结果（2026-09-06）

- Release x64 `/W4 /WX` 构建通过，UTF-8 源码编译支持中文文本。
- UI 自检退出码 0：中英文设置即时切换、实际托盘菜单文本、语言/口径写入独立
  测试配置并重新读取均通过，原有字号布局、非法输入和隐藏检查仍通过。
- 真实传感器测试退出码 0，20 个逻辑核心的 Time 和 Utility 计数器均有有效读数。
- Core3 固定负载测试（每种 7 秒）：
  - 65% 占空比工作负载下，当次 Time=55.00%，Utility=83.36%，显示总占用 83.36%。
    占空比是负载脚本目标，并非操作系统测量保证值。
  - 满负载下，Time=100.00%，Utility=138.00%，显示总占用 100.00%，内核占用 1.92%。
  - 证实两种口径在此 CPU 上有明显差异，默认 Utility 与 Windows 10 任务管理器的
    计数口径一致；未宣称与截图中的历史采样时刻逐值一致。
- 新版未完成截图验收：上一轮 Windows 捕获接口报告 E_NOINTERFACE。
  本轮通过同一菜单和控件对象的行为测试及基于布局网格的计算验证。
- 发布目录不包含测试日志、临时脚本或独立测试配置。

# 1.4.0 测试结果（2026-09-07）

## 环境与能力

- Windows 10，Intel Core i7-12700KF，Family 6 Model 97h，20 个逻辑处理器。
- 识别 12 个物理核心：8 个 P 核（LP0/1、2/3、…、14/15 配对），4 个 E 核（LP16–19）。
  每个逻辑处理器恰好属于一个物理核心和封装；P/E 由 CPUID 类型确认。
- 两种核心均报告 PMU v5、3 个 48-bit fixed counters，但通过当前 IntelMSR 签名模块
  读取 FIXED_CTR0 返回 Win32 5。IPC 与 LLC 指标明确不可用，未尝试写 PMU 或接管其他工具的计数器。
- 核心温度、Package 温度、RAPL 功率读取通过。一次最终读数 Package 45°C、65.11 W；
  这些是当时的实时值，并非准确度校准或额定参数。
- IntelMCHBAR 0.2.11 加载通过，0xD858/0xD8A0 与 +0x10000 的 bank 有效；
  0x5058/0x50A0 与 +0x10000 返回全 1，在启动后被跳过。

## 功能验证

- Release x64 `/W4 /WX` 构建通过。
- tests/hardware_tests.exe 通过：32-bit 回绕、长暂停重新建基线、非法位宽拒绝、
  孤立尖峰不报警、持续负载变红、恢复滞回、暂停后的告警重置、真实拓扑覆盖与 SMT 映射。
- --ui-test 退出码 0：中英文菜单/设置、配置读写、最大条目字体后的有效条宽、
  全部分组的逻辑处理器覆盖、E 组不超过 6 个物理核心、非法透明度回退、隐藏及退出。
- 默认 Busy Time；1.3.0 的 Utility 配置不会自动成为本版本默认，新设置可手动选择 Utility。
- 更新后的 computer-use 仍两次返回 `SetIsBorderRequired failed: 不支持此接口 (0x80004002)`。
  已做程序化布局验证，但未完成新界面的截图/人工视觉验收，也未实测多显示器 DPI 迁移。

## 性能与带宽对照

测量为进程内 GetProcessTimes、GetProcessMemoryInfo 和 QPC。每次先预热 3 秒，再测量
20 秒，浮窗可见，默认 250 ms。包含该进程的用户态/内核态 CPU，不包含 DWM 等其他进程
或 GPU 内存。采样延迟为整轮采样的墙钟耗时，会受调度/其他负载影响；发布到绘制延迟
只测到 WM_PAINT 开始，不是屏幕实际 scan-out 延迟。

本机同时有其他桌面程序运行，下列“无附加负载”不是系统完全空闲：

| 场景 | 进程 CPU（单核 / 20 LP 整机） | 工作集 / Private | 采样均值 / P95 | 发布→开始绘制 P95 |
| --- | --- | --- | --- | --- |
| 无附加内存负载，采样节奏调整前 | 0.703% / 0.035% | 44.07 / 62.86 MiB | 17.304 / 40.035 ms | 0.367 ms |
| 最终采样节奏，独立内存复制负载 | 1.015% / 0.051% | 44.08 / 63.25 MiB | 36.911 / 92.578 ms | 0.486 ms |

最终负载测试：20.01 秒中 76 次采样（约 3.8 Hz），144.4 次只读 IOCTL/秒。
这台机器每轮为 20 次频率 + 12 次核心温度 + 2 次封装指标 + 4 次有效 IMC 读取。
官方模块没有批量接口，因此没有声称已经合并为单次 IOCTL。

带宽验证使用独立 Python/ctypes 进程、两个 128 MiB 缓冲区（超过 LLC），延迟启动后
连续 memmove 12 秒。负载 CPU 不计入监控进程 CPU，但内存访问会影响监控调度。

- 无附加负载的一次测量：平均读 8.085 / 写 1.280 GB/s。
- 最终复制对照：平均读 11.697 / 写 7.182 GB/s，峰值读 18.676 / 写 12.198 GB/s。
- 计数器对独立内存流量有明显响应；测试包含工作负载前后的窗口，平均值不是纯负载稳态。
- memcpy 用户字节数、DRAM 事务字节数和系统总流量不能一一等同，此测试并非与外部
  带宽仪器/Intel PCM 的同步绝对值校准。

这是有限时长测试，不代表长时间压力认证。其他 SKU、BIOS、主板及 Intel/AMD 架构仍需
单独验证。IPC/LLC 的后端缺失和截图验收限制不因构建通过而视为解决。


## v1.5.0 - 2026-09-07

- VS 2026 v145 / Windows SDK 10.0.28000 Release x64 编译通过（/W4 /WX）。
- hardware_tests：Intel i7/i9/Core Ultra 与 AMD Ryzen 标题简化、计数回绕、暂停重建基线、异常消抖、12 物理核 / 20 LP 拓扑回归通过。
- --ui-test 与 --sensor-test 返回 0：中英文设置与菜单、浮窗标题/宽度语言不变、模式高度变化、两种预设、10 Hz / 4 Hz 映射、选项卡显隐、内容持久化、P 核零行距、字号边界、双击隐藏通过。
- RAM 开关实机验证通过：关闭后每轮 34 次 IOCTL，开启后 38 次；重新启用首轮不输出跨停用期间的累计速率。
- RAM 默认显示实际读写 GB/s，已删除固定 100 GB/s 标尺及比例条。未新增对最大带宽的估算。
- 官方 PawnIO 最新稳定安装包 2.2.0 从项目路径完成重新安装：卸载返回 0，安装返回 0；驱动状态 Running。
- 安装包 Authenticode Valid，SHA256 1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032；随附安装脚本 PowerShell 语法检查通过。
- IPC / LLC 仍受签名 IntelMSR 模块能力限制，PMU 探测返回 5；默认隐藏，手动勾选显示 N/A。

默认详细模式 + RAM 开启，4 Hz，3 秒预热后测量 20 秒：

| 指标 | 实测 |
| --- | --- |
| CPU 时间 | 0.078 秒 |
| CPU 占用（单核心 / 整机 20 LP） | 0.391% / 0.020% |
| 工作集 / 私有内存 | 44.10 / 63.37 MiB |
| 采样 | 77 次，约 3.85 Hz |
| IOCTL | 146.3 次/秒 |
| 采样墙钟耗时 mean / p95 | 11.023 / 39.607 ms |
| 数据发布到开始绘制 p95 | 0.329 ms |
| RAM 平均 R / W | 7.734 / 1.275 GB/s |

有限时长测量包含系统调度波动，不代表所有负载或 10 Hz 下的开销保证。
当前会话未提供原生 Computer Use 所需 node_repl 入口；本轮完成程序化 UI 验证，未完成截图视觉验收。

# ETW PMU 独立验证（2026-10-06；应用仍为 v1.5.0）

- 管理员权限下，系统 WPR 成功启用 InstructionsRetiredFixed、UnhaltedCoreCyclesFixed、LLCMisses，20 个逻辑处理器均有非零计数差值。
- 两次短时录制分别包含 1,418,755 / 855,430 条 PMC 事件，ETL 的 EventsLost 均为 0；停止后均确认 WPR 未录制。
- P/E 核计算和 256 MiB 内存访问对照显示，内存负载下 IPC 下降、LLC miss 增加。E 核原始 LLC MPKI 数值较高，仍需事件映射及 P/E 口径核对，不把本次访问验证视为完整指标精度认证。
- ETL 元数据的计数器顺序与 WPRP 声明不同，分析器按元数据名称匹配，排除跨负载阶段的差值。
- 未安装或编写新驱动、未修改启动安全设置、未接入监控程序。独立诊断与完整结果见 `scripts/research/ETW_PMU_RESULTS.md`；实时采集开销和产品集成尚未验证。

# v1.6.1 ETW 实时接入验证（2026-10-06）

- Windows 10 19045、i7-12700KF；既有 MSVC v145 / Windows SDK 10.0.28000.0 构建通过，警告视为错误。
- 新增 `tests/pmu_tests.vcxproj`：计数下降/回绕、零周期、暂停长间隔、非法时间、异常数量、未支持平台降级测试通过。
- 普通权限 `pmu_tests --denied` 通过，返回 ERROR_NOT_ALL_ASSIGNED (1300)，输出保持不可用，不修改系统权限。
- 管理员 `pmu_tests --live` 通过：全 20 个逻辑处理器产生有效数据；三次 10 秒模式测试各有 30 次完整有效发布快照；同名竞争返回 ERROR_BUSY，已有会话继续运行；IPC-only、关闭/重开、强制退出子进程后的遗留会话恢复、退出后自身 logger 不存在均通过。
- 最终产品回归 `--pmu-test` / `--ui-test` / `--sensor-test` 均 exit=0；硬件测试保持 12 物理核 / 20 LP 覆盖与 SMT 映射通过。PMU 集成测试累计 71 次有效采样，结束时 IPC=20/20、P 核 MPKI=15.0828、PMU_error=0。
- 开启 ETW 的 20 秒产品测量约整机 CPU 0.136%（单核 2.711%），工作集 44.65 MiB，private 63.14 MiB；采样平均 17.982ms / p95 62.693ms，发布到开始绘制 p95 0.841ms。另一次 60 秒检查测得整机 0.074%。背景负载不同，不能把差异当成优化或固定开销承诺；工作集不包含内核 ETW 缓冲。
- v1.6.1 在相同后端基础上统一版本、增加可见版本标题；最终人工视觉检查由用户确认 OK。Computer Use 截图接口重试后仍超时，未生成截图证据；布局按文字测量和现有模式/预设/缩放关系计算，程序化 UI 回归通过。
- LLC 仅标注/汇总 P 核；E 核 IPC 接入，E 核 LLC MPKI 不作为已校准指标发布。正常运行不写 ETL 文件；显示项关闭时释放 ETW，失效数据显示 N/A，故障退避重试。
- Windows 10 19045 实机不支持 TracePmcSessionInformation（返回 50）；直接 API 路径按自己提交的 source 列表顺序处理，较新系统查询或配置事件可以交叉核对。初版等待文件模式配置元数据的路径曾超时，已替换，不计作最终通过结果。
- 本轮没有现场注入 ETW 丢失事件，也没有实际挂起/恢复整台电脑；丢失检测和长间隔重建基线分别通过代码检查及单元测试覆盖。未把这些未实施场景记为实机通过。

证据保存于被 Git 忽略的 `build/final-app-tests.txt`、`build/final-benchmark.txt`、`build/final-ui-test.txt`、`build/final-sensor-test.txt`、`build/pmu-live-tests.txt`。发布文件不包含测试日志、配置、ETL 或中间构建产物。

发布包复验：直接运行 `Release/v1.6.1/Cpu_freq_Moniter_v1.6.1.exe --pmu-test`，exit=0；62 次有效采样，结束时 IPC=20/20、P 核 MPKI=11.8598、PMU_error=0。20.19 秒测量整机 CPU 0.105%、工作集 44.83 MiB，采样平均 7.603ms / p95 23.177ms。诊断日志已移至 `build/release-1.6.1-benchmark.txt`，发布目录的临时诊断文件已清理。

# v1.6.2 温度单位修复验证（2026-10-06）

- 单核温度格式从 `%.0f°` 改为 `%.0f°C`；列宽基于 `100°C` 实际文字测量，沿用 P/E 核分组布局和窗口宽度约束。
- MSVC 警告视为错误构建通过；`--ui-test` exit=0，模式/预设/刷新/内容/持久化/P 核间距回归通过。
- 已打开标注版本的独立检查窗口并向用户发出人工视觉检查提醒；本条不预先声明人工验收通过。
- 用户人工视觉检查确认：显示正常，无重叠或截断。

# v1.7.0 进阶 TMA 与精简布局验证（2026-10-06）

- 既有 MSVC v145 / SDK 10.0.28000，警告视为错误构建通过，无新驱动/工具链。
- PMU 单元测试通过：原有计数下降/暂停/非法时间检查，以及 TMA 零 slots、零 BadSpec、不一致余数 N/A；普通权限测试返回 1300，保持不可用。
- 首次实机测试返回 ERROR_BUSY (170)：正在运行的 v1.6.1 已占用 PMU；没有停止它或接管资源。用户明确退出旧版后重新测试。
- 管理员 `pmu_tests --tma` exit=0，43 次有效 TMA 发布；TMA+IPC+LLC、普通 IPC、TMA+IPC 无 LLC、关闭均通过。
- 产品 `--tma-test` exit=0：63 次有效 TMA、IPC/MPKI 同时采集；结束窗口 slots=58869360150，Retiring=12.857%、Frontend=35.532%、BadSpec=0%、Backend=51.611%、Core=11.691%、Memory=39.919%。这些是背景负载的实验性原始事件比值，不是四类精度认证；BadSpec 仍未验证。
- 20.18 秒测量整机 CPU 0.136%、工作集 45.43 MiB（不含内核 ETW 缓冲）、采样均值 11.400ms / p95 50.253ms。不能当成固定开销承诺。
- 新版系统 WPT WPR 注册/取消专属命名实例 exit=0；Persist=false 事件在本次开机枚举中保留，重启清除。本机系统自带旧版 WPR 注册失败，已明确记录依赖；没有将其记为可用。
- UI 回归 exit=0：进阶/精简模式切换、TMA 浮窗显隐、ViewMode 持久化、很小参数、现有语言/字体/内容/刷新回归。精简按物理核心合并名称/频率，保留线程条/IPC 和原高度；未更改条形数据口径。
- L1/L2/L3/DRAM 未采集，显示 N/A。Memory Bound 标记实验性，Frontend/Core 标记估算，负余数不强行归一化。
- 证据：build/tma-lifecycle-test.txt、tma-product-test.txt、tma-product-ui-test.txt。已打开很小预设的进阶 LIVE 与精简 DEMO 窗口等待人工验收，尚未将人工验收记作通过。

最终补充：WPR 产品注册帮助函数实测 status=0、全部 TMA 源可枚举；最终产品 TMA/UI 回归 exit=0。十五分钟检查副本使用与最终构建一致的二进制；精简 P 核条组恢复设置条高并增加 2 个实际像素，保持核心行组高度，等待最终人工确认。
五分钟进阶检查自动结束时 PMU_error=0，264 次有效 TMA 发布，整机 CPU 0.073%、工作集 94.74 MiB、private 95.76 MiB（不同于短时测量，未据此声明长期内存稳定性）。计数器精度、9Ah 型号和真实整机挂起恢复仍未认证；本轮未做 ETW 丢失故障注入。

人工最终反馈：精简 P 核每条增加约 5 个实际像素后，用户确认“条高合适，可以打包”。TMA 的 N/A 已确认仅为尚未采集的 L1/L2/L3/DRAM，不是 Backend 下 Memory Bound 的读取失败。TMA 比例依然标注实验性；未将此反馈扩大为指标精度认证。


# v1.8.0 L1/L2/L3/DRAM Bound（2026-10-13）

- 使用现有 MSVC v145 / SDK 10.0.28000，Release x64 /W4 /WX 构建通过。没有安装或编写驱动。
- 独立负载实测：固定 P LP0，16 KiB、512 KiB、8 MiB、128 MiB 的随机依赖链；成功 trace 1,789,216 个 PMC 记录，EventsLost=0、malformed=0，计数响应及原始证据见 scripts/research/MEMORY_BOUND_RESULTS.md。
- 发现 HAL 分配受限事件到错误 PMC 时 L1/L2 为零；调整为六个 generic、三个受限 stall 源最后提交后有效。本机结果不能作为其他硬件或 Windows 版本的分配保证。
- 单元检查通过：零周期、有效全零停顿、L1 原公式 max(...,0)、负层级差 N/A、异常超界；原有计数重置、暂停、非法时间及不支持平台检查继续通过。
- 普通权限 IPC 与 TMA 均返回 1300，未输出有效比例。管理员生命周期 exit=0：TMA+IPC+LLC、普通 IPC、TMA+IPC 无 LLC，槽位及周期组中的 peer 冲突、关闭后缓存清空通过；120 次有效顶层采样，两个进阶阶段均获得四项 Memory Bound。
- 收紧空闲零周期样本的处理：零周期不再误清空另一组 TMA 缓存；P 核真实中断/重置才使 P 核缓存失效，E 核不影响 P 核 TMA。轮换全程持有 owner，保留 12 秒 TMA / 2 秒 IPC 数据时限。
- 最终产品 --tma-test exit=0：81 次顶层、60 次四项 Memory Bound 有效采样；结束时 L1=10.754%、L2=3.266%、L3=8.819%、DRAM=22.934%，Memory 年龄 1.56 秒，上方槽位数据年龄 7.08 秒。IPC=20/20，LLC MPKI(P)=6.5951，PMU_error=0。这是当时全体 P 核背景活动，不是精度认证。
- 同次 20.20 秒测量：整机 CPU 0.321%、工作集 46.35 MiB、private 63.12 MiB；采样 mean 21.219ms / p95 84.254ms。轮换可能短暂重建基线；未宣称长期内存或开销保证。
- UI 程序化回归 exit=0：模式、预设、字体、持久化、语言、P 核间隔及 TMA 浮窗显隐。已重新打开最终构建的很小预设供人工检查；本轮人工反馈尚未收到，不记作视觉验收通过。
- 退出后 own session query=4201，释放通过。证据：build/memory-final-lifecycle.txt、memory-final-exits.txt、verify-memory-v180/benchmark.txt、memory-final-cleanup-after.txt、memory-ui-exit.txt。
- Bad Speculation 仍遵循用户要求显示未验证原始值；E 核 TMA、Model 9Ah 和真实挂起恢复未实机认证。

# v1.8.1 CoreScope 开源准备与改名（2026-10-15）

- 使用既有 VS Build Tools / MSVC v145 / SDK 10.0.28000，CoreScope.sln Release x64 /W4 /WX 重建通过；硬件与 PMU 测试工程构建通过。
- hardware_tests.exe / pmu_tests.exe 均 exit=0；覆盖原有拓扑、SMT、计数差、异常时间、TMA/Memory Bound 计算边界。
- CoreScope.exe --version / --help 均 exit=0；Windows 文件属性 ProductName=CoreScope，FileVersion / ProductVersion=1.8.1，OriginalFilename=CoreScope.exe。
- --ui-test exit=0：在 build 内独立临时目录验证旧配置复制、已有新配置优先和旧文件保留，并通过模式/预设/刷新/内容/持久化/中英文/P 核间隔及进阶显隐回归。
- --sensor-test exit=0：真实 IMC、温度和功耗采集可用，RAM 开关/重新建基线检查 PASS。所有诊断仍使用独立配置。
- --tma-test exit=0：81 次有效顶层 TMA、61 次有效四项 Memory Bound、69 次有效 IPC/LLC 采样，PMU_error=0 / IMC_error=0。最终一次 IPC 处于轮换重建基线窗口，0/20、LLC N/A；未据此宣称全程无间断。
- 同次背景负载测量 20.19 秒，整机 CPU 0.344%，工作集 46.32 MiB、private 64.21 MiB，采样 mean 23.145ms / p95 195.087ms。这些是当时实机观察值，不是性能承诺。
- 普通权限 pmu_tests --denied exit=0，IPC/TMA 以 1300 降级；管理员 --cleanup exit=0，退出后专属会话 query=4201（不存在），清理通过。
- 当前与 8 个现有提交中的相关源码/文档已进行常见凭据模式检查，无命中；未读取任何私钥。官方模块 SHA-256 与固定值一致，模块源码归档包含 IntelMSR.p、IntelMCHBAR.p 和 COPYING。
- 本轮没有可调用的桌面图形检查接口；程序化 UI 回归不等同于截图或人工视觉验收。采集算法和控件布局保持原实现，仅更名与迁移配置；其他平台与 Bad Speculation 的验证边界仍适用。
- 诊断证据：build/x64/Release/ui-test.txt、sensor-test.txt、benchmark.txt 和 build/corescope-pmu-cleanup.txt；诊断日志、配置和中间产物不进入新发布目录。
