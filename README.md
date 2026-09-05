# CPU Frequency Monitor 1.0.0

Windows 10/11 x64 上的 Intel CPU 每逻辑处理器主频监视器。此版本不再使用 PerfLib；内核驱动在对应逻辑处理器上读取 `IA32_PERF_STATUS`（MSR `0x198`），用户态 CLI 每秒显示一次当前倍频乘以 CPUID 报告的总线频率。

## 限制与安全设计

- 当前版本仅支持 Intel CPU，并要求 CPUID leaf `0x16` 提供总线频率。
- 驱动 IOCTL 只返回固定的频率结构，不接受 MSR 编号，也不提供任意 MSR 读写。
- 安装、启动和卸载内核驱动需要管理员权限；驱动正常运行后，普通用户可打开只读查询接口。
- x64 Windows 必须接受驱动签名。开发构建需要测试签名和启用测试模式；公开分发需要 Microsoft 签名。
- `IA32_PERF_STATUS` 是瞬时性能状态读数，不是基于 `IA32_APERF/IA32_MPERF` 时间差计算的 Effective Clock。

## 构建

要求：

- Visual Studio 2026 Build Tools，Desktop development with C++
- Windows SDK 10.0.28000
- Windows Driver Kit 10.0.28000

```powershell
msbuild CpuFreq.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

构建输出在 `build\x64\Release`。正式发布目录为 `Release\v1.0.0`，只包含运行所需文件和文档。

## 安装与使用

驱动必须先具备 Windows 可接受的签名。随后在管理员命令提示符中运行：

```text
install.cmd
```

再运行：

```text
Cpu_freq_Moniter_v1.0.0.exe
```

按 `Ctrl+C` 正常退出。卸载驱动时，在管理员命令提示符运行 `uninstall.cmd`。

## 输出

```text
[12:34:56.789] CPU00=5200 MHz  CPU01=5200 MHz  CPU02=4900 MHz
```

内部保留 Windows Processor Group 与组内处理器编号，显示时按组和组内编号扁平化为 CPU00、CPU01……。
