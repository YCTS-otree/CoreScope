# CPU Frequency Monitor 1.1.0

Windows 10/11 x64 上的 Intel CPU 每逻辑处理器主频监视器。程序通过已获
Microsoft 硬件兼容签名的 PawnIO 驱动加载官方 `IntelMSR.bin` 模块，在各逻辑
处理器上读取 `IA32_PERF_STATUS`（MSR `0x198`），不再携带自行开发的 `.sys`
驱动，也不需要开启测试模式。

## 运行要求

- Intel x64 CPU；当前针对现代 Intel 客户端平台的 100 MHz 基准时钟。
- Windows 10/11 x64。
- 安装 PawnIO 2.x 官方签名版：https://pawnio.eu/
- 程序启动时需要管理员授权，这是 PawnIO 的设备访问策略。
- `Cpu_freq_Moniter_v1.1.0.exe` 与 `IntelMSR.bin` 必须放在同一目录。

本项目不会使用 CPU-Z、RTCore64 等第三方软件的私有或高风险驱动接口。
PawnIO 驱动会验证模块签名；官方 Intel 模块只允许访问明确列入白名单的 MSR。

## 构建

要求：Visual Studio 2026 Build Tools、Windows SDK 10.0.28000。自有驱动已经
移除，因此构建应用本身不再要求 WDK。

```powershell
msbuild CpuFreq.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

构建输出在 `build\x64\Release`。程序持续每秒采样，按 `Ctrl+C` 退出：

```text
Cpu_freq_Moniter_v1.1.0.exe
```

只采样一次后退出：

```text
Cpu_freq_Moniter_v1.1.0.exe --once
```

输出示例：

```text
[12:34:56.789] CPU00=5200 MHz  CPU01=5200 MHz  CPU02=4900 MHz
```

`IA32_PERF_STATUS` 是瞬时倍频读数，不是基于 `IA32_APERF/IA32_MPERF` 时间差
计算的 Effective Clock。

## 第三方组件

`third_party/PawnIO.Modules/IntelMSR.bin` 来自 PawnIO.Modules 0.2.11 官方发布，
采用 GNU LGPL 2.1-or-later。详情见同目录的 `README.md` 和 `COPYING`。
