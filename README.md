# CPU Frequency Monitor 1.7.0

轻量常驻 CPU / 内存性能浮窗，默认显示真正的 **Busy Time（% Processor Time）**。
目前已验证的硬件传感器平台为 Intel Alder Lake（Family 6，Model 97h / 9Ah）；
其他平台保留系统拓扑和 PDH 占用率，尚未验证的硬件指标显示 N/A。

## 运行

退出旧版托盘实例，再运行 `Release/v1.7.0/Cpu_freq_Moniter_v1.7.0.exe`。
需要管理员权限。发行包附带官方签名 PawnIO 2.2.0 安装器，
首次使用请在管理员 PowerShell 运行 `Dependencies/PawnIO/Install-PawnIO.ps1`。
需要使用随附包替换已有驱动时添加 `-Reinstall`；本机已完成替换安装。
PawnIO 是系统共享驱动，并非每个项目私有的驱动实例。`IntelMSR.bin`、`IntelMCHBAR.bin` 必须与 exe 放在一起。

- 单实例 IPC：再次启动显示已有浮窗，新进程退出。
- 全窗拖动、双击隐藏；托盘单击设置，右键详细/精简/进阶模式、显示/隐藏、设置、关于、退出。
- 设置和托盘菜单支持 English / 简体中文；浮窗文本始终为 English。设置即时预览并保存在 `%LOCALAPPDATA%/CpuFreqMonitor/settings.ini`。
- 浮窗自动适应逻辑核数、字体和 DPI，过高时滚轮查看；长 CPU 名称可以换行。
- 升级至本版默认 4 Hz 刷新，继续使用 Busy Time，保留旧的外观及语言配置。
  Utility 仍可手动选择，其内核段使用配套的 Privileged Utility。

## 指标与能力边界

| 指标 | 当前实现 |
| --- | --- |
| 逻辑处理器 / 物理核心 / SMT sibling | Windows GetLogicalProcessorInformationEx，一次识别并缓存 |
| P / E 类型 | 在目标逻辑核上读取 CPUID.1Ah，不跑负载、不按频率猜测 |
| Core ID | UI 显示 Windows 枚举的物理核心序号，诊断另列 x2APIC / hardware core ID |
| CPU 名称 / PMU 能力 | CPUID brand、family/model、每逻辑核的 PMU version / fixed counter 数量与宽度 |
| Busy Time / 内核占用 | PDH Processor Information 的 Processor Time / Privileged Time |
| 频率 | IA32_PERF_STATUS 瞬时倍频，不是 Effective Clock |
| 核心温度 | 每物理核心读取 IA32_THERM_STATUS，启动时缓存 TjMax；SMT 不重复读取 |
| Package 温度 | IA32_PACKAGE_THERM_STATUS，独立于最高核心温度 |
| Package Power | RAPL Package Energy 差值 / QPC 实际时间，处理 32-bit 回绕 |
| DRAM Read / Write | 官方 IntelMCHBAR 模块读取 IMC free-running counters，显示实际 GB/s；不设置带宽上限 |
| IPC | Windows ETW / PMC fixed instructions 与 unhalted core cycles；每逻辑处理器约 1 秒更新 |
| LLC MPKI (P) | Windows ETW LLCMisses / 同窗口 P 核 retired instructions × 1000；E 核不纳入 |

P 核按物理核心分组显示 SMT 线程；E 核最多 6 个物理核心一组，并列显示各核心温度。
蓝色是总占用，深蓝色覆盖其中的内核占用，不相加。详细模式显示百分比和频率；IPC 栏可选。P 核子条贴合，间隔为 0 DIP，条高至少容纳文字。
精简模式按物理核心显示一次 `P 0` / `E 8` 标签和一次频率，SMT 的占用条上下贴合，P 核每条比设置值增加 5 个实际像素（限于现有核心行组内），IPC 分开，保留物理核心之间的间隔和原来的每物理核心总高度。频率取同一物理核心的第一个有效 sibling 读数；不平均为虚构频率。
标题只保留厂商和型号，例如 `Intel i7-12700KF`。

没有使用 APERF 冒充 retired instructions，也没有使用 Pages/sec、缓存缺页或累计计数
冒充 LLC MPKI / DRAM 带宽。IPC 与 P 核 LLC MPKI 使用真实 ETW 计数；没有有效数据时显示 N/A。
在设置中勾选 IPC 或 LLC MPKI 后启动独立 ETW 会话；普通模式两项均关闭后释放；进阶模式另外启用 TMA，共用一个 ETW 会话。仅在已验证的 Intel Alder Lake Family 6 Model 97h/9Ah 启用；需要管理员性能分析权限。初始化/冲突/丢失/超时会降级并显示错误，30 秒后重试，普通 CPU/传感器采集继续。见 `ARCHITECTURE.md`。

## 进阶模式 / TMA（实验性）

TMA 本轮实机仅验证 i7-12700KF（Model 97h）的 P 核；9Ah 同族配置尚未实机验证。

进阶模式保留详细主浮窗，额外打开独立、可拖动的 P 核 TMA 面板，位置单独保存，透明度/字号/置顶跟随设置。隐藏主浮窗时同时隐藏 TMA，切换回普通模式释放 TMA 事件；关闭 TMA 面板返回详细模式。

按全体 P 核同一窗口的槽位增量加权汇总，不包含 E 核，也不是特定应用的性能分析。所有百分比均以 P 核总 slots 为分母。
- Retiring、Backend Bound 来自原始计数器；Frontend Bound 为余数估算。
- Bad Speculation 依用户要求显示原始比值，标注未验证；曾在分支压力测试下全为零，不能把 0% 当成已确认“没有错误推测”。
- Backend 下增加 Core Bound（Backend − Memory 的估算）与 Memory Bound（实验性事件）。负的余数显示 N/A，不强行归一化。
- Memory 下 L1/L2/L3/DRAM 暂未采集，明确显示 N/A，不能用 LLC MPKI 换算这些层级比例。

自定义事件首次启用需要支持扩展 PMU 注册的 WPR。本机 Windows SDK 中的 WPR 10.0.28000 已验证；系统自带 10.0.19041 的注册失败。程序优先使用已安装的 Windows Performance Toolkit WPR，再尝试系统 WPR；不会安装工具。`tma-register.wprp` 必须与 exe 同目录。注册使用本程序专属命名实例和 Persist=false，不保存 ETL，注册描述在本次开机期间保留，重启后消失。

TMA 与 IPC/LLC 共用原有独占 owner 与 ETW 会话；资源冲突不停止别的工具。TMA 注册/配置失败时回退普通 IPC/LLC，显示 TMA 错误并每 30 秒重试。计数下降、长间隔、丢失、数据过期均降级为 N/A。实验性范围不等于指标精度认证。

## DRAM 带宽

按用户澄清，保留实际 RAM R/W 读数，删除不准确的固定 100 GB/s 上限与比例条。
默认显示读写 GB/s 数值，不推断内存带宽饱和程度。
关闭显示时不初始化或读取 IMC；重新启用时先建立计数基线。
初始化最多执行一次，缓存有效 bank；按实际间隔计算低 32 位计数差，长暂停重新建基线。

## 异常颜色

- Busy Time EMA 达 90% 为橙色、98% 为红色，仅改变 Busy 数字和条，不改变频率或整行。
- 核心 / Package 温度距有效 TjMax 15°C / 5°C 时分别橙色 / 红色。
- 检测到当前 Thermal Throttling 状态时温度立即红色，解除后有恢复延迟。
- EMA 时间常数 1.5 秒，普通升级需持续 1.5 秒，恢复需持续 2 秒且跨过滞回阈值。
- IPC / MPKI 不可用时不染异常色；没有自动断言“CPU 瓶颈”或“内存瓶颈”。

## 设置

| 设置 | 默认 | 范围 |
| --- | --- | --- |
| 不透明度 | 92% | 15–100% |
| 宽度 | 340 DIP，内容不足时扩展 | 220–1200 DIP |
| 行距 | 5 DIP | 0–40 DIP |
| 标题 / 条目字号 | 19 / 13 DIP | 10–48 / 8–36 DIP |
| 条高 | 12 DIP | 2–48 DIP |
| 内边距 / 圆角 | 14 / 12 DIP | 4–48 / 0–40 DIP |
| 刷新率 | 4 Hz | 1–10 Hz |
| 置顶 | 1 | 0 / 1 |

100% 缩放时 1 DIP 对应 1 像素。采样与浮窗刷新共用所选频率，实际频率受采样耗时影响。

设置包含「常规」「显示内容」选项卡。常规可选详细/精简/进阶模式、小号/默认/很小预设；
手动调整参数仍有效。小号/默认预设调整尺寸与字体；“很小”按用户参数设置：92%、宽 250、行距 1、标题 16、条目 9、条高 6、内边距 10、圆角 10、刷新 1 Hz、置顶开启。均不覆盖语言或内容选择。
显示内容可分别开关 IPC、RAM R/W、LLC MPKI、频率、温度、封装功耗（详细模式）。
IPC、LLC MPKI 默认隐藏，RAM R/W 默认显示。LLC 明确标注 P 核范围；E 核 LLC 口径尚未确认，不纳入整机估算。

## 构建和测试

使用现有 VS 2026 Build Tools v145、Windows SDK 10.0.28000、C++17，无新增工具链。

```powershell
msbuild CpuFreq.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
msbuild tests/hardware_tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\tests\hardware_tests.exe
```

- `--once`：保留原有一次性频率采样入口。
- `--demo`：明确标记的 CPU 模拟演示；不可用的 PMU 和带宽仍不伪造。
- `--ui-test`：语言、配置、分组覆盖、字号边界、隐藏交互自检。
- `--sensor-test`：真实传感器诊断，含 topology 和模块能力错误码。
- `--benchmark`：3 秒预热后测量 20 秒 CPU、内存、采样墙钟耗时、发布到开始绘制延迟。
- `--pmu-test`：独立配置启用 IPC/LLC，预热后测量 20 秒；`--pmu-inspect`：对应约 5 分钟的独立可见检查。
- `--tma-test`：20 秒 TMA + IPC + LLC 实机测试；`--advanced-inspect`：约十五分钟进阶实机检查；`--compact-inspect`：约十五分钟精简布局 DEMO（IPC 明确为模拟值）。
- `--inspect`：独立无托盘的 63 秒可见检查窗口，自动退出。

自检/测量输出写到 exe 旁的测试文件，使用独立窗口类与配置，不修改用户设置、不创建托盘。
开发产物位于 `build/`，发布包含 exe、签名模块、官方 PawnIO 安装器及文档/许可证；测试结果见 `TEST_RESULTS.md`。
当前图形捕获接口不可用，不能将程序化布局检查等同于完成截图验收。

## 第三方组件

PawnIO.Modules 0.2.11，LGPL-2.1-or-later，来源和哈希见
`third_party/PawnIO.Modules/README.md`；发布目录附带许可证与来源说明。
官方 PawnIO 安装器来源、SHA256 和重装方法见 `Dependencies/PawnIO/README.md`（源码中位于 `third_party/PawnIO.Setup/`）。
开启 IPC/LLC 时由 Windows ETW 管理 PMU；不直接写 PMU/IMC 寄存器，不启用 unrestricted 模式，不需要 Windows 测试模式或新驱动签名。
