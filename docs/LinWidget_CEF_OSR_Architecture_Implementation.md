# LinWidget CEF OSR 离屏渲染与层级架构实现总结

## 1. 架构目标与核心突破
在桌面环境中，传统的 WebView2 / CEF 原生子窗口（HWND）存在以下致命缺陷：
1. **Native 窗口层级遮挡**：原生子窗口始终会遮挡 Qt 绘制的桌面图标（ListView Items）及桌面格子（LinBox）；
2. **分层窗口冲突与参数错误**：使用 `WA_NativeWindow` 分层窗口时，Windows 的 `UpdateLayeredWindowIndirect` 频繁报错 `(The parameter is incorrect)`；
3. **窗口生命周期管理复杂**：销毁原生 Browser 容易引发 CEF 引用计数崩溃或宿主销毁析构顺序错误。

**最终方案突破**：
通过 **CEF OSR（Off-Screen Rendering，离屏渲染）+ QPainter 统一绘制管线**，成功实现了将组件系统完美嵌入在桌面图标下方，且完全不受 Win32 HWND 层级穿透的困扰。

---

## 2. 核心架构原理与层级设计

### 2.1 统一绘制管线（小组件在图标下方）
将 CEF 浏览器输出的像素帧缓冲统一汇聚到 `LinWidgetCanvasWidget`，并在 `ListView::paintEvent` 中按照明确的 Z-Order 顺序执行单次统一渲染：

```
+-------------------------------------------------------+
|  1. 静态系统壁纸 (LinDesktop 背景绘制)                  |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|  2. CEF OSR 小组件画布 (m_canvasWidget->renderCanvas)   |
|     - 包含默认居中毛玻璃卡片（default_page.html）       |
|     - 包含所有用户运行的 Web 小组件                      |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|  3. 桌面图标与格子元素 (ListView Root Items & Box)      |
|     - paintDesktopRootItemDynamic / Static            |
|     - 鼠标框选、重命名、快捷方式与高亮文本              |
+-------------------------------------------------------+
```

### 2.2 关键实现要点

1. **纯软件帧缓冲管理（去除 WA_NativeWindow）**：
   - `LinWidgetCanvasWidget` 仅作为一个纯内存图像合成器与事件转发器，不创建任何独立的 Windows 原生子窗口 HWND，彻底杜绝了 `UpdateLayeredWindowIndirect failed` 崩溃。
2. **CEF 客户端引用解耦**：
   - `CefOsrBrowserInstance` 拥有自己独立的生命周期管理，在 `close()` 时显式解绑并置空宿主浏览器引用，避免在析构与事件循环中重复释放导致崩溃。
3. **原生桌面的干净开关控制**：
   - 启用 CEF OSR 时：彻底关停并清理 WebView2 与动态壁纸，由 CEF OSR 接管，无组件时居中显示毛玻璃默认卡片；
   - 禁用 CEF OSR 时：彻底注销所有 CEF 浏览器实例，清理画布缓冲帧 `clearAllFrames()`，显式通过 Win32 `SetParent` 保证 `ListView` 依附在 `LinDesktop` 上，完美恢复原生静态壁纸与桌面图标。

---

## 3. 涉及的核心代码模块

| 文件 | 职责 |
| :--- | :--- |
| `src/desktop/widgets/cef/CefOsrBrowserInstance.h/.cpp` | 离屏 CEF 浏览器实例管理，处理帧回调与输入事件传递 |
| `src/desktop/widgets/cef/LinWidgetCanvasWidget.h/.cpp` | 内存画布，管理多组件帧混合，执行命中测试与点击穿透 |
| `src/desktop/widgets/linwidget/WidgetManager.cpp` | 组件系统生命周期调度，默认页更新，动态壁纸隔离 |
| `src/desktop/ListView.cpp` | `paintEvent` 中集成 `renderCanvas`，实现组件在图标下方的分层绘制 |
| `src/resources/LinWidgetRuntime/default_page.html` | CEF OSR 默认居中毛玻璃“你好”引导页面 |
