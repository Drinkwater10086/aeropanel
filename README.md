# 三角洲行动自动切换分辨率

Windows PowerShell 脚本，按开发顺序保留八个版本。前四个版本来自当时保存的归档，部分被覆盖的中间版本无法还原。

1. `01-manual`：手动启动，固定切换 2880×2160 并恢复 3840×2160。
2. `02-auto-fixed`：后台检测，使用固定分辨率。
3. `03-auto-4x3`：按主显示器高度计算 4:3，保存并恢复原模式。
4. `04-multi-platform`：增加多平台进程检测。
5. `05-tray-settings`：新增设置界面、托盘菜单、自定义分辨率和错误日志；[改动与性能试测](05-tray-settings/CHANGELOG.md)。
6. `06-performance`：优化空闲轮询和显示代码加载；[性能对比](06-performance/PERFORMANCE.md)。
7. `07-native`：原生托盘监测后台；[使用与性能试测](07-native/README.md)。
8. `08-single-exe`：设置、托盘和切换均为原生单文件；[使用说明](08-single-exe/README.md)。

## 使用最新版本

先停止旧版后台并关闭旧版自启，再双击 `08-single-exe/DeltaResolution.exe`。在设置窗口选择平台和目标分辨率，检查后保存。详细步骤、数据保存位置及已知限制请见[单文件版本说明](08-single-exe/README.md)。

已完成模拟流程测试，真实游戏和分辨率切换尚未完成端到端实机验证。WeGame 可能在游戏进程出现后才触发；支持的模式由显示驱动决定。
