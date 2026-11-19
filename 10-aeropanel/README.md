# AeroPanel v3.0.1

使用和构建说明见 [仓库首页](../README.md)。当前目录包含原生 C++ 源码、PresentMon 2.3.1 内嵌数据及许可、便携构建脚本、69 项回归测试与匿名化的帧事件回放样本。发行二进制在 GitHub Releases 下载。

主程序：`.build/AeroPanel.exe`。测试程序：`.build/SuiteTests.exe`，结果：`.build/test-results.txt`。

测试程序的 `--live-display` 是显式实屏测试，会改变主屏模式并恢复，同时启动延迟恢复保护；默认构建测试不执行该项。请先保存工作再手动使用。`--restore-guard` 与 `--probe-child` 为内部测试参数。


### 图形 API 标签

通过设备创建事件的实际调用栈识别 D3D9、D3D10、D3D11、D3D12、OpenGL 和 Vulkan。建议先启动 AeroPanel，再启动游戏。托盘菜单的「增强 API 识别（管理员）」会保存选项，并只为检测模块请求管理员权限；Windows 可能显示 UAC。权限不足、缺少调用栈或游戏已经运行时，仍可能只能显示 DXGI 或 FPS。多 API 路径显示组合标签。详见 TESTING.md 中的实际测试范围。
