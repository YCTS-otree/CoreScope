# CPU Frequency Monitor 1.4.0

轻量常驻 CPU / 内存性能浮窗，默认显示真正的 **Busy Time（% Processor Time）**。
目前已验证的硬件传感器平台为 Intel Alder Lake（Family 6，Model 97h / 9Ah）；
其他平台保留系统拓扑和 PDH 占用率，尚未验证的硬件指标显示 N/A。

## 运行

退出旧版托盘实例，再运行 `Release/v1.4.0/Cpu_freq_Moniter_v1.4.0.exe`。
需要管理员权限和已安装的 PawnIO 2.x 官方签名驱动。发布目录中的
`IntelMSR.bin`、`IntelMCHBAR.bin` 必须与 exe 放在一起。

- 单实例 IPC：再次启动显示已有浮窗，新进程退出。
- 全窗拖动、双击隐藏；托盘单击设置，右键显示/隐藏、设置、关于、退出。
- English / 简体中文；设置即时预览并保存在 `%LOCALAPPDATA%/CpuFreqMonitor/settings.ini`。
- 浮窗自动适应逻辑核数、字体和 DPI，过高时滚轮查看；长 CPU 名称可以换行。
- 升级至本版默认恢复 Busy Time 和 250 ms 采样，保留旧的外观及语言配置。
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
| DRAM Read / Write | 官方 IntelMCHBAR 模块只读 IMC free-running counters，64 bytes/transaction，十进制 GB/s |
| IPC | **N/A：当前签名 IntelMSR 模块拒绝 fixed PMU counter 读取** |
| LLC Miss / Reference / MPKI | **N/A：当前模块未开放 PMU 事件配置与计数读取** |

P 核按物理核心分组显示 SMT 线程；E 核最多 6 个物理核心一组，并列显示各核心温度。
蓝色是总占用，深蓝色覆盖其中的内核占用，不相加。每条线程显示百分比、频率和 IPC 栏。

没有使用 APERF 冒充 retired instructions，也没有使用 Pages/sec、缓存缺页或累计计数
冒充 LLC MPKI / DRAM 带宽。IPC 和 LLC 并未实现为可用数据，不能据此做性能诊断。
后续要启用它们，需要提供 PMU 访问及资源协调能力的可信后端；见 `ARCHITECTURE.md`。

## DRAM 带宽

采用 Intel PCM 对 Alder Lake 选择的 TGLClientBW 读取方式。启动时识别可用 MMIO bank，
跳过读写均为全 1 的未实现区域；正常运行仅读取已确认的 bank，不写 IMC 配置。
每个方向汇总事务增量乘 64，并除以实际采样秒数与 1e9。

使用低 32 位模差；间隔超过 2 秒时该轮带宽显示 N/A 并重新建立基线，以免多次回绕
产生错误速率。RAPL 在长暂停后也会重新建立基线。错误或不合理的读数不进入显示。
RAM 条的标尺固定为 **100 GB/s**，旁边明确注明；不是测得的系统最大带宽，也不据此判定饱和。

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
| 采样间隔 | 250 ms（约 4 Hz） | 100–5000 ms |
| 置顶 | 1 | 0 / 1 |

100% 缩放时 1 DIP 对应 1 像素。设置过慢的采样间隔会使 IMC 速率不可用，见上文。

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
- `--inspect`：独立无托盘的 63 秒可见检查窗口，自动退出。

自检/测量输出写到 exe 旁的测试文件，使用独立窗口类与配置，不修改用户设置、不创建托盘。
开发产物位于 `build/`，发布只包含 exe、签名模块及文档/许可证；测试结果见 `TEST_RESULTS.md`。
当前图形捕获接口不可用，不能将程序化布局检查等同于完成截图验收。

## 第三方组件

PawnIO.Modules 0.2.11，LGPL-2.1-or-later，来源和哈希见
`third_party/PawnIO.Modules/README.md`；发布目录附带许可证与来源说明。
本程序不安装其他驱动、不修改 PMU/IMC 配置、不需要 Windows 测试模式。
