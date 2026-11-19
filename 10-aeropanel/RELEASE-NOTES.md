# AeroPanel v3.0.1

- 新增银狼图标与可关闭的预模糊设置背景。
- 图表右侧显示当前值，紧凑布局移除重复读数。
- Windows 桌面使用 GDI 浮层的 1 Hz 刷新周期，显示 1 FPS、1000 ms，平均和 1% low 均为 1 FPS。
- 新增可选管理员 API 识别。先启动 AeroPanel，再启动游戏；证据不足时显示通用标签。

《三角洲行动》在先前管理员构建中实测识别为 D3D12。后续构建调整了采集与模块映射的权限分工，尚未重新验证该游戏；集成复测曾漏报 D3D9、OpenGL 和 Vulkan。游戏帧率依赖 Windows 事件采集，采集丢失时可能无法显示。

硬件传感器读取 MSI Afterburner。帧率使用内嵌的 PresentMon。更多测试范围见 TESTING.md。
