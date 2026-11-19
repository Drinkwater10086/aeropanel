# AeroPanel v3.0.0

使用和构建说明见 [仓库首页](../README.md)。当前目录包含原生 C++ 源码、PresentMon 2.3.1 内嵌数据及许可、便携构建脚本、62 项回归测试与匿名化的帧事件回放样本。发行二进制在 GitHub Releases 下载。

主程序：`.build/AeroPanel.exe`。测试程序：`.build/SuiteTests.exe`，结果：`.build/test-results.txt`。

测试程序的 `--live-display` 是显式实屏测试，会改变主屏模式并恢复，同时启动延迟恢复保护；默认构建测试不执行该项。请先保存工作再手动使用。`--restore-guard` 与 `--probe-child` 为内部测试参数。
