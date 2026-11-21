# AeroPanel v3.0.8

使用和构建说明见 [仓库首页](../README.md)。当前目录包含原生 C++ 源码、PresentMon 2.3.1 内嵌数据及许可、便携构建脚本、106 项回归测试与匿名化的帧事件回放样本。发行二进制在 GitHub Releases 下载。

主程序：`.build/AeroPanel.exe`。测试程序：`.build/SuiteTests.exe`，结果：`.build/test-results.txt`。

测试程序的 `--live-display` 是显式实屏测试，会改变主屏模式并恢复，同时启动延迟恢复保护；默认构建测试不执行该项。请先保存工作再手动使用。`--restore-guard` 与 `--probe-child` 为内部测试参数。


### 图形 API 标签

通过设备创建事件的实际调用栈识别 D3D9、D3D10、D3D11、D3D12、OpenGL 和 Vulkan。建议先启动 AeroPanel，再启动游戏。托盘菜单的「增强 API 识别（管理员）」会保存选项，并只为检测模块请求管理员权限；Windows 可能显示 UAC。权限不足、缺少调用栈或游戏已经运行时，仍可能只能显示 DXGI 或 FPS。多 API 路径显示组合标签。详见 TESTING.md 中的实际测试范围。

### 帧率采集路径

| 图形接口 | 首选 | 备用 |
|---|---|---|
| D3D9 / D3D9Ex | PresentMon | Windows D3D9 Present 开始事件（1） |
| D3D10 / D3D11 / D3D12 | PresentMon | Windows DXGI Present 开始事件（42、178） |
| OpenGL / Vulkan | PresentMon | 没有独立备用采集，系统事件不可用时保留 N/A |
| HTML5 视频 | Video FPS 扩展 | 浏览器的视频帧回调，度量视频而非窗口 |
| Windows 桌面 | 浮窗固定刷新周期 | GDI 1 Hz |

Direct3D 备用采集按前台进程或指定 PID 筛选，分别处理各交换链和事件版本，避免重复计算。统计口径是画面提交间隔。CS2 和《地平线 4》已实测详细 DXGI 事件；旧 DXGI 和 D3D9 路径仍需逐游戏确认。没有向游戏注入代码。

### 硬件采集器构建

使用 .NET 10 SDK、Windows x64 C++ 构建工具和 Windows SDK。在本目录运行：

```powershell
dotnet publish src/BoardVoltage.csproj -c Release -r win-x64 -p:PublishAot=true -p:UseAppHost=true -o .build/board-voltage
```

把输出中的 BoardVoltage.exe、libMonoPosixHelper.dll 和 MonoPosixHelper.dll 放到 AeroPanel.exe 同级目录下的 runtime/board-voltage/。运行时需要 LibreHardwareMonitor 所用的 PawnIO 驱动才能访问相关硬件。CPU Vcore 当前仅映射到已验证的 GIGABYTE B650M AORUS ELITE AX ICE 的 IT8689E Voltage #1。发布包不含驱动安装器。

快捷键可通过 AeroPanel.ini 的 [Hotkey] 节配置，Key=121 表示 F10，Modifiers=3 表示 Ctrl + Alt。修饰键掩码为 Alt=1、Ctrl=2、Shift=4、Win=8。