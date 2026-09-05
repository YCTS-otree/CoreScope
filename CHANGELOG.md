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
