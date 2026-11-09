### LinDesk v1.4.0 更新日志（2026-11-02）

#### 1. LinWidget 2.0 纯 Web 原生开放架构与物理分流
- **彻底物理分流解耦**：统一采用 `.lw` 压缩包规范，安装器（`WidgetPackage::installPackage`）按 `manifest.json` 中 `type: "widget" | "wallpaper"`（兼容旧 `role`）分流解压归档到专属目录 `%APPDATA%/LinDesk/LinWidgets/<id>/` 与 `%APPDATA%/LinDesk/LinWallpapers/<id>/`，实现组件与壁纸两类资产彻底物理隔离。
- **正交元数据模型**：动态壁纸元数据解耦为 `wallpaperType: "web" | "video" | "picture"` 与 `interactive: true | false`，精准指导宿主鼠标转发策略与工坊渲染器选择。
- **多媒体流式协议支持**：新增 `linwidget://media?path=` 流式协议，支持高速平滑读取并播放外部大体积视频壁纸，无需拷贝即可畅快渲染。

#### 2. 标准现代 window.LinDesk 原生系统能力扩展
- **极简三合一接口**：摒弃繁重专有 RPC，数据原生直用 `localStorage`，网络直用 `fetch`，宿主仅注入轻量标准 `window.LinDesk`（Promise + 事件监听回调风格）：
  - `LinDesk.system.getMetrics()`：异步获取 CPU、内存(MB/总计/百分比)、电池、网络上下行速率及 Uptime 快照；
  - `LinDesk.system.onMetrics(callback)`：开启 1000ms 周期硬件监控推送，返回 `unlisten()` 取消订阅句柄；
  - `LinDesk.shell.openPath(filePath)`：资源管理器安全定位目录或高亮选中文件；
  - `LinDesk.shell.openUrl(url)`：调用默认外部浏览器打开网页链接（严格限制 `http://` 与 `https://` 协议）；
  - `LinDesk.widget.close()` / `openSettings()` / `resize(w, h)`：普通小组件容器控制能力；
  - `LinDesk.wallpaper.onPropertyChange(callback)`：壁纸工坊参数变动实时监听器。

#### 3. 物理级安全硬拦截与沙箱防护
- **底层可执行文件硬拦截**：`LinDesk.shell.openPath` 建立底层物理拦截黑名单，物理级严禁唤起 `.exe`、`.bat`、`.cmd`、`.ps1`、`.vbs`、`.msi`、`.com`、`.scr` 等任何可执行文件与脚本，切断恶意程序静默唤起路径。
- **壁纸容器误关隔离**：壁纸运行沙箱中严格禁用 `LinDesk.widget` 容器操作（注入为 `null` 并在 C++ 宿主侧拦截），杜绝壁纸误调导致桌面背景异常黑屏。
- **防目录穿越防护**：本地资源加载强制规范化目标路径并进行根目录包含性校验，严格阻断 `../../` 跨包或越权访问磁盘目录。

#### 4. 极客独立开发工作流与热重载
- **本地零依赖开发**：任何前端开发者在本地直接双击 `index.html` 即可在 Chrome/Edge 中调试排版。
- **源码文件夹热识别**：支持将组件/壁纸源码文件夹直接放进 `LinWidgets/` 或 `LinWallpapers/` 即刻热识别生效。
- **一键呼出 Chromium DevTools**：桌面小组件右键菜单挂接「开发者调试工具 (F12)」，直调 Chromium 完整开发者工具。
- **按键原生热重载刷新**：按键拦截原生支持 <kbd>Ctrl</kbd>+<kbd>R</kbd> 与 <kbd>F5</kbd>，直接触发 CEF `ReloadIgnoreCache` 无缓存热重载。

#### 5. 动态壁纸工坊 (LinDesk Hub) 独立大窗口实装
- **1120×720 Fluent 架构独立窗口**：右键菜单新增 `🎨 动态壁纸与工坊...` 及设置中心直达大卡片入口。
- **16:9 实时物理画板与图标浮层**：提供 16:9 画板实时渲染、桌面图标透视浮层以及声明式 properties 动态参数调整（滑块、颜色、布尔开关）。

#### 6. 自动化测试与工程质量
- 新增 `WidgetManifestTests` 与 `WidgetPackageTests` 单元测试，覆盖 2.0 Manifest 解析、壁纸正交属性序列化与物理安装目录分流验证。
- CTest 全量 9 项测试用例（`SystemInfo`、`WidgetEnvelope`、`LogManager`、`Persistence`、`RuntimeBridge`、`RuntimeInstaller`、`StoreAppIconCompat`、`DisplayNameOverride`、`ShellMenuWorker`）100% 顺利通过。

---

### LinDesk v1.3.0 更新日志

#### 1. 盒子视觉特效与外观增强
- **多级景深与视觉效果**：新增分层 Z 轴景深、自适应壁纸色彩增强以及活跃拖拽发光脉冲。
- **标题栏与布局优化**：修复盒子收起/展开时内容区域残留与刷新不同步问题，标题栏收起按钮与右侧关闭按钮、网格对称对齐。
- **拖拽指示器现代化**：重构盒子内外部拖拽指示线与悬停发光效果，支持清晰的壁纸采样背景。

#### 2. 桌面基础操作与视觉动效
- **拖拽 Ghost 多选上限扩展**：桌面图标多选拖拽预览数量上限由 5 个放宽至 20 个，避免常规多选过早折叠为计数徽章。
- **现代流光框选（Glass Marquee）**：全新 Win11 Fluent 风格半透明圆角选框与实时多选动态徽章（HUD Badge），支持动态圆角与边框粗细微调。
- **流体隐藏与显示动效**：重构桌面元素显示/隐藏过渡动画为 60fps 平滑流体动效，消除卡顿。

#### 3. 设置中心全面现代化
- **模块化导航面板**：重构设置中心为 Fluent 2.0 侧边栏导航面板，将常规、桌面、盒子、小组件、右键菜单、插件、关于等按模块深度解耦。
- **FluentSlider 控件体系**：全栈接入顺滑拖拽与微调的 FluentSlider 滑块控件，解决原生 QSlider 卡滞问题。
- **平滑滚动与性能优化**：引入 SmoothScrollArea 配合脏矩形优化，大幅降低设置页切换能耗与闪烁。

#### 4. 系统级稳定性与兼容性
- **Win11 右键菜单深度对齐**：右键菜单项“下一个桌面背景”图标与行为精准同步原生 Windows 11 上下文菜单。
- **托盘图标常驻保护**：托盘图标长效保持可见，新增右键打开设置，并优化单实例冲突恢复弹窗与底层进程保护机制。
- **小组件交互增强**：支持 Ctrl 快捷键对桌面小组件进行原地缩放与自由拖拽，优化 CEF OSR 离屏渲染层级管线。
