# CPU Frequency Monitor 1.3.0

Windows 10/11 x64 的 Intel CPU 桌面浮窗监视器。使用原生 Win32、Direct2D 和
DirectWrite 绘制界面，通过 PawnIO 官方签名驱动读取每逻辑处理器的瞬时频率。

## 使用

运行 `Release/v1.3.0/Cpu_freq_Moniter_v1.3.0.exe`，按提示允许管理员权限。
`IntelMSR.bin` 必须与 exe 位于同一目录。需要安装 PawnIO 2.x 官方签名版。

- 每行对应一个逻辑处理器；蓝色长度表示总使用率，深蓝色覆盖其中的
  内核使用率，两者不相加。默认采用 Processor Utility 性能使用率，超过 100% 时按 100% 显示。
- 频率以 GHz 显示；温度是所有有效核心读数中的最高温度。
- 浮窗任意位置可拖动，双击隐藏。高度超过屏幕时可用滚轮查看剩余内容。
- 托盘图标单击打开设置；右键提供显示/隐藏、设置、关于、退出。
- 再次启动会通过单实例 IPC 显示已有浮窗，新进程退出。
- 设置即时预览并保存到 `%LOCALAPPDATA%/CpuFreqMonitor/settings.ini`，位置也会保存。
- 无法访问硬件或计数器时显示 N/A，不以模拟读数代替真实数据。

## 语言与占用率口径

设置中的 Language / 语言支持 English 和简体中文，即时更新设置、托盘菜单、
关于窗口和状态文字，自动保存。没有语言记录时按系统 UI 语言选择，非中文系统使用英文。

CPU 占用率口径提供：

- 性能使用率 / Processor Utility（默认）：使用 `% Processor Utility` 与
  `% Privileged Utility`，与 Windows 10 任务管理器的口径一致。睿频时原始值
  可能超过 100%，进度条限制在 0–100%。
- 时间占用率 / Busy time：使用 `% Processor Time` 与 `% Privileged Time`，
  表示采样期间忙碌的时间比例，保留 1.2.0 的原始口径。

两种口径不会混用内核计数器，计数器不可用时显示不可用状态，不静默切换口径。
任务管理器与本程序的采样时刻可能不同，因此瞬时读数不保证完全相同。
微软对两种口径的说明：https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/cpu-usage-exceeds-100

## 可调设置

| 设置 | 默认值 | 范围 |
| --- | --- | --- |
| 不透明度 | 92% | 15–100% |
| 窗口宽度 | 340 DIP | 220–1200 DIP |
| 行距 | 5 DIP | 0–40 DIP |
| 标题字号 | 19 DIP | 10–48 DIP |
| 条目字号 | 13 DIP | 8–36 DIP |
| 使用率条高 | 12 DIP | 2–48 DIP |
| 内边距 | 14 DIP | 4–48 DIP |
| 圆角半径 | 12 DIP | 0–40 DIP |
| 采样间隔 | 1000 ms | 250–5000 ms |
| 置顶 | 1 | 0 / 1 |

DIP 为 100% 缩放时的像素大小，跟随显示器 DPI 缩放。为避免文字与进度条重叠，
实际宽度会在所设宽度不足时自动增大。无效输入在离开输入框时恢复上次有效值。

## 数据来源

- 频率：PawnIO + Intel `IA32_PERF_STATUS`，优先 CPUID.16H 提供的总线频率，
  未提供时采用现代 Intel 客户端平台的 100 MHz 基准。属于瞬时倍频，非 Effective Clock。
- 占用率：Windows PDH `Processor Information` 的 Utility / Time 计数器对；
  按 Processor Group 与逻辑处理器编号匹配。
- 温度：Intel `IA32_THERM_STATUS` 的有效温差与 `IA32_TEMPERATURE_TARGET`
  的 TjMax 计算，不支持或读取失败时显示 N/A。
- P/E/SuperP 识别计划在后续版本实现，当前不自动分类。

## 构建与验证

需要 Visual Studio 2026 Build Tools（v145）、Windows SDK 10.0.28000 和 C++17。
使用系统现有工具，无需 WDK 或额外 UI 框架。

```powershell
msbuild CpuFreq.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

开发输出位于 `build/x64/Release`，分发文件位于 `Release/v1.3.0`。

- `--once`：一次频率采样，保留命令行兼容用途；从脚本捕获输出时需管理员权限。
- `--demo`：明确标有 DEMO 的模拟传感器预览；与正常 UI 共享单实例锁。
- `--ui-test`：独立运行模拟数据 UI 自检，不创建托盘、不写用户配置，完成后退出。
- `--sensor-test`：同样执行 UI 自检，并记录真实传感器读数。

两种自检将日志及独立测试配置写入可执行文件旁的 `ui-test.txt` / `sensor-test.txt`
和相应 `.txt.ini` 文件，需要该目录可写。测试不会修改用户配置。
不要把自检日志纳入分发目录。测试覆盖与限制见 `TEST_RESULTS.md`。

## 第三方组件

`IntelMSR.bin` 来自 PawnIO.Modules 0.2.11 官方发布，采用 GNU LGPL 2.1-or-later。
分发目录包含其许可和来源说明。PawnIO 驱动需单独安装：https://pawnio.eu/
