# AeroPanel v3.0.5

- 扩展 Direct3D 备用采集：增加 D3D9 和传统 DXGI Present 事件，移除 CS2 与《地平线 4》的进程白名单。
- 优先使用 PresentMon。备用采集按目标进程、交换链和事件类型区分样本。
- 采集开启且持续无有效 FPS 时，图表与设置页提示反馈。托盘菜单可打开 GitHub Issue 页面。

OpenGL 和 Vulkan 继续使用 PresentMon。新版的传统 DXGI 和 D3D9 备用路径尚未逐游戏实测。Video FPS 扩展仍为 v1.1.3。
