# Changelog

## v1.0.0 - 2026-09-06

### Added
- 添加 Intel `IA32_PERF_STATUS` 内核驱动和受限的只读频率查询 IOCTL。
- 添加支持 Processor Group 的用户态持续采样 CLI。
- 添加管理员安装、卸载脚本及独立版本化发布目录规范。

### Changed
- 放弃在当前硬件上始终返回固定 3600 MHz 的 PerfLib 实现，主分支转为驱动架构。

### Notes
- 驱动加载需要 Windows 可接受的数字签名；开发阶段实机验证需要测试签名环境。

## v1.1.0 - 2026-09-06

### Added
- 添加对 PawnIO 官方签名驱动和 `IntelMSR.bin` 0.2.11 模块的直接调用。
- 添加 `--once` 一次性采样模式。

### Changed
- 主频读取后端由自有未签名驱动改为 PawnIO，不再要求 Windows 测试模式。
- 应用启动时请求管理员权限，以符合 PawnIO 的设备访问策略。

### Removed
- 移除无法在普通 Windows 签名策略下加载的自有 `.sys` 驱动及其安装脚本。

### Notes
- PawnIO 2.2.0 的系统驱动已在测试机验证为 Microsoft Windows Hardware Compatibility Publisher 签名。

## v1.1.1 - 2026-09-06

### Fixed
- 严格区分模块文件大小查询失败与无效大小，避免返回陈旧的 Win32 错误码。

### Notes
- 最终管理员实机测试读取到 P 核逻辑线程 5000 MHz、E 核逻辑线程 4000 MHz。
