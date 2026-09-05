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
