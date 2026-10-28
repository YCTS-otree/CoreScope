# CoreScope

**轻量常驻的 Windows CPU 与内存性能监视器。**

[English](README.en.md) · [下载发布版](https://github.com/YCTS-otree/CoreScope/releases) · [更新记录](CHANGELOG.md) · [GPL-3.0](LICENSE)

CoreScope 将每核活动、硬件传感器与性能计数器放进可拖动的桌面浮窗。它由 CPU Frequency Monitor 演进而来，适合在日常使用和性能实验中持续观察处理器状态。

当前版本：**1.9.3**。采用 C++17、Win32、Direct2D / DirectWrite，支持详细、精简和实验性的进阶模式。

## 功能

- **核心拓扑与占用**：识别物理核心、逻辑处理器、SMT 和 Intel P/E 核；分别显示总占用与其中的内核占用。
- **传感器**：瞬时频率、核心 / 封装温度、RAPL 封装功耗，以及实际 DRAM 读写带宽。
- **性能计数器**：可选的每逻辑处理器 IPC、P 核 LLC MPKI，使用 Windows ETW / PMC 采集。
- **进阶 TMA 面板**：观察 P 核 Retiring、Frontend / Backend Bound、Bad Speculation，以及 Core / Memory Bound 和 L1 / L2 / L3 / DRAM Bound。
- **常驻浮窗**：托盘控制、置顶、透明度、字体、大小、显示项和 1–10 Hz 刷新率可调；支持 DPI 缩放与超高窗口滚动。
- **语言**：设置与托盘支持简体中文 / English，指标浮窗使用英文。

## 支持范围

| 项目 | 当前范围 |
| --- | --- |
| 操作系统 | Windows x64；实机验证环境为 Windows 10 22H2（19045） |
| 硬件传感器 / IPC / LLC | Intel Alder Lake，Family 6、Model 97h / 9Ah；主要实测处理器为 i7-12700KF |
| AMD 传感器 | Zen Family 17h / 19h / 1Ah：P-state 频率、封装温度与能量差分功耗；本轮实测 Ryzen AI 9 HX 370（1Ah / 24h），其他 AMD 型号待实机验证 |
| TMA | 实验性，目前仅 i7-12700KF 的 P 核经过实机验证；9Ah 配置尚未实机验证 |
| 其他处理器 | 保留系统拓扑与 PDH 占用率；未支持的硬件指标显示 `N/A` |
| 权限与驱动 | 程序要求管理员权限；硬件传感器使用官方签名的 PawnIO 驱动 |

AMD 使用 Windows 物理核心 / SMT 分组，不将 Zen 5 / Zen 5c 标为 Intel P/E 核。逐核温度、RAM R/W、IPC、LLC MPKI 和 TMA 暂未适配 AMD，显示 `N/A`；精简模式温度取封装传感器。多封装 AMD 不读取仅定位 bus 0 的温度接口。无 PawnIO 或模块缺失时，AMD 频率回退到 Windows 报告值，并明确标注来源；该值可能仅反映操作系统的性能状态，不等于瞬时或有效频率。

Windows 11 已完成 HX 370 的有限时长检查，其他处理器与系统组合仍需验证。CoreScope 暂无 GPU、风扇、SPD 或特定进程分析功能。

## 下载与运行

1. 从 [Releases](https://github.com/YCTS-otree/CoreScope/releases) 下载发布包，或使用本地构建生成的 `CoreScope_v1.9.3_x64.zip`，完整解压到可写目录。
2. 若未安装 PawnIO，在**管理员 PowerShell** 中进入解压目录并运行：

   ```powershell
   .\Dependencies\PawnIO\Install-PawnIO.ps1
   ```

   随包提供固定版本的官方 PawnIO 2.2.0 安装器；脚本先校验 SHA-256 与签名，再安装。已安装时无须重装。
3. 退出旧版 CPU Frequency Monitor / CoreScope 托盘实例，运行 `CoreScope.exe` 并接受管理员权限提示。

请将 `IntelMSR.bin`、`IntelMCHBAR.bin`、`AMDFamily17.bin`、`tma-register.wprp` 与程序放在同一目录。PawnIO 为系统共享驱动，其他硬件监视工具也可能使用它。

**进阶模式的额外要求**：TMA 自定义事件注册需要支持扩展 PMU 注册的 Windows Performance Recorder。开发环境验证了 Windows SDK / Windows Performance Toolkit 的 WPR 10.0.28000；Windows 10 自带的 10.0.19041 注册失败。CoreScope 优先使用已安装的 WPT WPR，不会自动安装工具。TMA 不可用时降级显示错误，常规监控继续工作。

## 使用

- 在浮窗任意位置拖动；双击隐藏，滚轮滚动较高的窗口。
- 托盘单击打开设置；右键切换详细 / 精简 / 进阶模式、显示 / 隐藏，或退出。
- **详细模式**显示每逻辑处理器占用与频率；**精简模式**按物理核心组织，保留 SMT 子条。
- **进阶模式**打开额外的 P 核 TMA 面板；关闭该面板返回详细模式。
- 蓝色为总占用，深蓝色为其中的内核占用，两者不相加。
- 默认使用详细模式；默认 / 小号 / 很小预设宽度为 250 / 230 / 210 DIP，字号、条高与留白同步缩小。旧原始默认尺寸会自动缩小，已有显示模式及自定义配置保留。
- Core 标题到自身线程条的间距也随行间距调节，不再保留固定的组内/组尾空隙。
- 行间距支持 -40 至 40 DIP 的负数和小数（如 `-1.5`、`0.25`），保存后保留小数；极端负值下布局至少保留 1 DIP 的行步进。
- 默认 4 Hz；IPC 与 LLC MPKI 默认关闭，可在显示内容中开启；关闭后释放相应 ETW 资源。

设置保存在 `%LOCALAPPDATA%\CoreScope\settings.ini`。首次运行会复制旧的 `%LOCALAPPDATA%\CpuFreqMonitor\settings.ini`；已有 CoreScope 配置优先，旧文件不会被删除或覆盖。复制失败时回退使用旧配置。

## 如何理解指标

| 指标 | 数据来源与含义 |
| --- | --- |
| Busy Time / 内核占用 | PDH Processor Time / Privileged Time；默认使用实际 Busy Time |
| 频率 | Intel `IA32_PERF_STATUS`；AMD `C0010293` 当前硬件 P-state，1Ah 使用 FID × 5 MHz，17h / 19h 使用 FID / DFSID × 200 MHz；**不是 Effective Clock** |
| 核心 / 封装温度 | Intel 数字温度传感器，核心温度按物理核心读取 |
| 封装功耗 | Intel RAPL / AMD Package Energy 差值除以实际采样时间；AMD 温度来自 SMN Tctl/Tdie，独立于功耗读取 |
| RAM R/W | IMC 实际计数差，单位 GB/s；没有固定带宽上限或饱和度判断 |
| IPC | ETW fixed retired instructions / unhalted core cycles，约每秒更新 |
| LLC MPKI (P) | 同一窗口的 P 核 LLC misses / retired instructions × 1000；不含 E 核 |

TMA 反映**全体 P 核的系统活动**，不属于某个应用。顶层比例以 slots 为分母；L1 / L2 / L3 / DRAM Bound 以 cycles 为分母，表示负载停顿归因，不是缓存命中率，也不是 Memory Bound 内部百分比，不能与顶层比例直接相加。

Frontend Bound 和 Core Bound 包含余数估算；**Bad Speculation 尚未验证**，显示 0% 不代表已经确认没有错误推测。计数器分组轮换，TMA 通常每 10 秒更新一轮，并显示数据年龄。无效、过期或不支持的数据显示 `N/A`，不会伪造读数或自动断言瓶颈。

更多实现与指标边界见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 从源码构建

需要 Git、Visual Studio 2026 Build Tools（MSVC v145、C++ 桌面开发组件）和 Windows SDK。工程自动选择已安装的 Windows 10/11 SDK；本轮使用 **10.0.26100.0**，原 Intel 环境使用 10.0.28000.0。TMA 的 WPR 运行时要求不变。使用现有工具链，不需要额外 Python、Node.js 或包管理器。

在 Developer PowerShell 中运行：

```powershell
git clone https://github.com/YCTS-otree/CoreScope.git
cd CoreScope
msbuild CoreScope.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

产物为 `build\x64\Release\CoreScope.exe`，两个官方模块与 TMA profile 会自动复制到同目录。传感器运行时仍需要安装 PawnIO。模块与安装器按固定版本保存在 `third_party/`，构建不下载依赖、不编译或签名驱动。

### 验证

```powershell
msbuild tests/hardware_tests.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild tests/pmu_tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\tests\hardware_tests.exe
.\build\tests\pmu_tests.exe
.\build\tests\hardware_tests.exe --require-sensors
.\build\tests\hardware_tests.exe --missing-module
.\build\x64\Release\CoreScope.exe --ui-test
```

GUI 自检和真实硬件测试需要在可交互的管理员会话运行；测试输出写在 exe 旁，使用独立配置，不覆盖日常设置。

| 参数 | 用途 |
| --- | --- |
| `--help` / `--version` | 命令帮助 / 版本信息 |
| `--once` | 一次性 Intel / AMD 频率采样，与 GUI 共用后端 |
| `--demo` | 明确标注的演示浮窗，不伪造传感器 / PMU 数据 |
| `--ui-test` | 布局、语言、显示项、配置迁移和持久化自检 |
| `--sensor-test` | 真实硬件传感器诊断 |
| `--benchmark` | 3 秒预热 + 20 秒开销测量 |
| `--pmu-test` / `--tma-test` | ETW / TMA 实机检查 |
| `--inspect` / `--pmu-inspect` | 独立可见检查窗口，自动退出 |
| `--advanced-inspect` / `--compact-inspect` | 进阶实机 / 精简演示检查，约 15 分钟后退出 |
| `--compact-live-inspect` | 精简模式真实硬件检查，约 1 分钟后退出，使用独立配置 |

当前验证结果与限制见 [TEST_RESULTS.md](TEST_RESULTS.md)。

### 打包

构建和验证后运行：

```powershell
.\scripts\package.ps1
```

脚本从 `shared/version.h` 读取版本，并校验 exe 的版本元数据，将程序、依赖、许可、文档和校验和写入新的 `Release/v<版本>/`，生成对应 ZIP。已有发布目录不会被覆盖；历史发行包保留原名称。

## 项目结构

```text
app/          Win32 界面、采样与 ETW/PMU 后端
shared/       统一版本定义
tests/        硬件逻辑与 PMU 回归测试
scripts/      发布打包与独立研究工具
third_party/  固定版本的官方组件、许可证与模块源码
Release/      按版本分开的发行包
```

旧的 `CpuFreqMonitor` 单实例、ETW 和 WPR 内部标识有意保留，便于与旧版本互斥及恢复遗留会话。

## 反馈与贡献

欢迎通过 [Issues](https://github.com/YCTS-otree/CoreScope/issues) 报告问题或提交 Pull Request。硬件问题请附 CPU 型号、Windows 版本、CoreScope 版本、启用的模式及错误码；日志请先去除个人信息。新增平台需要独立确认寄存器、计数器语义和实机结果。

## 许可证与致谢

CoreScope 原创代码采用 **GNU General Public License v3.0 only（SPDX: GPL-3.0-only）**，全文见 [LICENSE](LICENSE)。Copyright © 2026 YCTS-otree and contributors.

第三方组件保留各自许可证，不因随包分发而改为 GPL-3.0。感谢 [PawnIO](https://pawnio.eu/) 与 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)；对应来源、许可证、校验信息与模块源码见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
