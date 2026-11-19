# 程序分辨率自动切换

Windows 分辨率自动切换工具。当前版本为原生单文件程序，支持指定任意 EXE，并保留三角洲渠道预设。按开发顺序保留九个版本。前四个版本来自当时保存的归档，部分被覆盖的中间版本无法还原。

1. `01-manual`：手动启动，固定切换 2880×2160 并恢复 3840×2160。
2. `02-auto-fixed`：后台检测，使用固定分辨率。
3. `03-auto-4x3`：按主显示器高度计算 4:3，保存并恢复原模式。
4. `04-multi-platform`：增加多平台进程检测。
5. `05-tray-settings`：新增设置界面、托盘菜单、自定义分辨率和错误日志；[改动与性能试测](05-tray-settings/CHANGELOG.md)。
6. `06-performance`：优化空闲轮询和显示代码加载；[性能对比](06-performance/PERFORMANCE.md)。
7. `07-native`：原生托盘监测后台；[使用与性能试测](07-native/README.md)。
8. `08-single-exe`：设置、托盘和切换均为原生单文件；[使用说明](08-single-exe/README.md)。
9. `09-general-program`：按完整路径监听其他程序，保留三角洲预设；[使用说明](09-general-program/README.md) · [v2.0.0 改动](09-general-program/RELEASE-NOTES.md)。

## 使用最新版本

从 [Release](https://github.com/Drinkwater10086/delta_restiming/releases/latest) 下载 `ResolutionSwitcher.exe`。先关闭旧版登录自启并退出旧版后台，再打开新版。选择“指定其他程序”，浏览选定实际运行的 EXE，或选择三角洲 Steam、官方启动器、WeGame 预设。选择分辨率，检查后保存。

通用模式按完整路径匹配当前 Windows 会话的程序，启动时切换、匹配实例全部退出后恢复。一次启用一个目标；当前会话中修改目标将在恢复后生效。设置界面关闭后仍可通过托盘打开。

详细升级步骤、数据位置和限制请见[通用版说明](09-general-program/README.md)。已完成使用模拟显示接口的流程测试；真实分辨率切换尚未完成端到端实机验证。除 Steam 外，其他三角洲渠道均未实机测试，本版本 Steam 完整流程也尚未实机验证。
