# LinDesk AI 代理约束（AGENTS.md）

> 本文件是所有 AI 代理（Antigravity、Cursor、Windsurf、Claude Code、Copilot 等）在参与 LinDesk 工程开发时的最高准则。

---

## 1. 工程硬约束

### 1.1 源文件编码
- 所有源文件（`.cpp / .h / .hpp / .cmake / .json / .md`）一律 **UTF-8（无 BOM）**；
- 禁止引入或保留乱码注释；发现历史乱码注释直接清理为正确 UTF-8 或删除；
- 新增文件必须显式以 UTF-8 写入；工具链输出（脚本重定向、生成文件）不得经过系统代码页转码。

### 1.2 日志与崩溃处理
- 日志库统一 **spdlog**（header-only），经 `qInstallMessageHandler` 转发；**禁止**引入 glog / Boost.Log / log4cplus；
- 崩溃捕获：主进程用 `SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，CEF 子进程用 CEF 自带 crash dumps；**禁止**完整集成 breakpad / crashpad；
- 多进程日志**分文件**（desktop.log / cef.log / updater.log …），统一行格式，禁止多进程共享同一日志文件句柄；
- 日志落盘目录统一 `%LOCALAPPDATA%/LinDesk/logs/`。

---

## 2. 前台 UI 与壁纸工坊架构铁律（Strict UI Development Rule）

> ⚠️ **警告（AI 绝对禁止项）**：
> 凡是涉及 **LinDesk Hub、壁纸工坊 (Wallpaper Studio)、壁纸画廊 (Gallery)、桌面组件中心 (Widgets)、能效状态机 (Power) 以及控制台 (Console)** 的界面需求，**100% 属于现代 Web 前端工程**！
> **绝对禁止**在 C++ 侧（如 `WallpaperStudioWindow.cpp` 或 Qt 窗口）手写、拼凑任何 QWidget / QPushButton / QLabel 等原生界面来代替 Hub 页面！

### 2.1 前端工程源码与技术栈
- **源码根目录**：`web/hub/`
- **设计标准**：**[shadcn/ui](https://ui.shadcn.com/)** 设计规范（浅色柔和沉浸美学、微边框、卡片阴影、Lucide Icons、平滑动效）。
- **技术栈**：React 18 + TypeScript + Vite + Tailwind CSS + Lucide Icons。
- **模块与文件映射**：
  - `web/hub/src/App.tsx`：应用主视图顶层调度、IPC 消息总线与 Tab 切换；
  - `web/hub/src/components/WallpaperStudio.tsx`：Master-Detail 二合一壁纸工坊（左侧 58% 搜索与流式画廊、拖拽一键导入，右侧 42% 16:9 实时物理画板、模拟桌面图标、作者专属动态属性表单、宿主通用外挂控制项）；
  - `web/hub/src/components/WidgetCenter.tsx`：桌面组件管理、脚手架创建与权限管理；
  - `web/hub/src/components/PowerView.tsx`：底座能效与 0.0% 挂起智能休眠状态机（AC 60 FPS / 电池 30 FPS / 全屏 3D 游戏 WasHidden 0% 挂起）；
  - `web/hub/src/components/DevConsole.tsx`：IPC 实时报文与日志监控；
  - `web/hub/src/services/ipc.ts`：前端与 C++ 后端双向通信 Bridge (`linwidget://bridge/postMessage`)。

### 2.2 UI 变更必须执行的标准工作流
AI 修改或新增 UI 页面与功能时，**必须严格按以下步骤闭环**：
1. **修改前端源码**：编辑 `web/hub/src/` 下的对应组件；
2. **构建前端产物**：在 `web/hub` 目录下执行编译：
   ```powershell
   cd web/hub
   npm run build
   ```
   构建产物会自动输出至 `src/resources/hub/`（`index.html` 与 `assets/`）；
3. **同步运行目录**：将构建生成的文件同步至运行输出目录：
   ```powershell
   # 确保 build/Release/resources/hub 同步最新构建产物
   Copy-Item -Path "src/resources/hub/*" -Destination "build/Release/resources/hub" -Recurse -Force
   ```
4. **C++ 与 Native 职责边界**：
   - C++（`WallpaperStudioWindow.cpp`）作为 CEF 容器宿主壳，必须且仅加载 `resources/hub/index.html`；
   - C++（`WidgetManager.cpp`）仅负责 Win32 穿透、CEF OSR 离屏渲染管理与 JSON-RPC 消息响应（`list`, `apply_wallpaper`, `set_wallpaper_property`, `stop_wallpaper`），严禁侵入 UI 呈现逻辑。

---

## 3. 原生客户端与设置中心 Fluent UI 设计规范（WinUI 3 Design Standard）

凡涉及 C++ 原生客户端界面（`src/ui/SettingUI.*`、`src/ui/settings/`、`src/ui/Fluent/` 及弹窗提示），必须 **100% 遵循 Windows 11 WinUI 3 Fluent Design 官方设计规范**，严禁私自捏造字重、背景色或使用原生 `QMessageBox`。

### 3.1 表面色彩与对比度层级（Light Mode Surface Hierarchy）
- **窗口主画布 (Mica Base)**：`#f3f3f3`（应用于 `#fluentBaseContainer`、`FluentNavigationPanel`、`#rightArea`、`#contentStack`、`SmoothScrollArea` 视口背景），**严禁使用近白底色（如 `#fbfcfe`）导致白卡与背景无对比度**；
- **一级设置卡片 (`SettingsCard` / `#settingsPanel` / `#homeTile`)**：纯白背景 `#ffffff`，外框 `1px solid #e5e5e5`，圆角 `8px`；
- **二级内嵌容器 (`#settingsInnerRow` / `#shellExcludeBody` / 列表框底色)**：微灰表面 `#f9f9f9` 或透明背景，描边 `1px solid #e5e5e5`，圆角 `6px`；
- **卡片内部分割线 (`#softDivider`)**：`1px` 实线 `#ebebeb`；
- **主题强调色 (Accent)**：主色 `#0067c0`（悬停 `#1975c5`，按下 `#005299`），柔和强调图标底色 `#eff6fc`。

### 3.2 字体与排版层级（Typography Scale）
- **字体族**：优先 `'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'`；图标统一使用 `'Segoe Fluent Icons', 'Segoe MDL2 Assets'`；**严禁使用 `font-weight: 800` 等臃肿字重**；
- **标准字号与字重**：
  - **页面主标题 (`#pageTitle`)**：`24px`，`font-weight: 600` (Semibold)，`color: #1b1b1b`；
  - **页面副标题 (`#pageSubtitle`)**：`13px`，`font-weight: 400` (Regular)，`color: #5c5c5c`；
  - **分组小标题 (`#cardTitle`)**：`14px`，`font-weight: 600` (Semibold)，`color: #1b1b1b`；
  - **设置项主标题 (`#rowTitle`)**：`14px`，`font-weight: 500` (Medium)，`color: #1b1b1b`；
  - **设置项副标题 (`#rowSubtitle`)**：`12px`，`font-weight: 400` (Regular)，`color: #5c5c5c`。

### 3.3 左侧导航栏规范（`FluentNavigationPanel`）
- 固定宽度 `216px`，背景与主窗口 `#f3f3f3` 无缝一体；
- 菜单项高度 `38px`，间距 `4px`，左右内缩 `10px`，圆角 `6px`：
  - **未选中态**：透明背景，`16px` `#1b1b1b` 图标 + `13px Regular #1b1b1b` 文本；
  - **悬停态**：`rgba(0, 0, 0, 0.04)` 浅灰填充；
  - **选中态**：`rgba(0, 0, 0, 0.06)` 中性灰填充（**严禁使用大面积高饱和蓝底**）+ 左侧 `3px × 16px` `#0067c0` 圆角竖条 + `#0067c0` 图标 + `13px DemiBold #1b1b1b` 文本。

### 3.4 设置子页面布局与弹窗规范
- **统一页面边距与头部结构**：所有设置子页面（`General`、`Desktop`、`ContextMenu`、`Box`、`LinWidgets`、`Plugins`、`About`）外层 `QVBoxLayout` 必须统一使用 `setContentsMargins(36, 12, 36, 32)`，顶部左对齐放置 `createPageTitle` + `4px` 间距 + `createPageSubtitle` + `20px` 间距后进入卡片内容区；
- **滚动容器托管**：所有设置子页面统一由 `SettingUI::wrapScrollablePage` 包装为 `SmoothScrollArea`，子页面内部严禁重复嵌套 `QScrollArea`；
- **按钮与弹窗**：页面内操作按钮统一使用 `SettingsUICommon::createSettingsActionButton`；客户端所有提示、告警与确认框一律使用 `FluentDialog`（`showInfo` / `showWarning` / `showError` / `confirm`），**绝对禁止调用 `QMessageBox`**。
