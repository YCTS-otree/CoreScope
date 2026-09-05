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
