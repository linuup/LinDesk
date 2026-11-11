# LinDesk (Windows 桌面整理与小组件增强平台)

[![Version](https://img.shields.io/badge/version-1.2.1%20Stable-blue.svg)](CMakeLists.txt)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-purple.svg)](CMakeLists.txt)
[![Framework](https://img.shields.io/badge/Framework-Qt%205.15.2-green.svg)](CMakeLists.txt)
[![Engine](https://img.shields.io/badge/Widget%20Engine-CEF%20OSR%20120%2B-orange.svg)](docs/LinDesk_Architecture_Guide.html)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20x86-lightgrey.svg)](docs/LinDesk_Product_Intro.html)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/Tests-8%2F8%20Passed-brightgreen.svg)](tests/)

LinDesk 是一款专为 Windows 平台打造的现代化桌面整理与 HTML 桌面小组件平台。在不替换系统原生 Explorer 桌面的前提下，LinDesk 挂载于 `WorkerW` 之上，提供自绘的高性能图标网格布局、可折叠与映射本地目录的桌面收纳盒（LinBox），以及基于 Chromium Embedded Framework (CEF OSR) 离屏渲染的无原生句柄桌面小组件（LinWidget）体系。

---

## 📑 核心特性与现状清单 (Implemented & Roadmap)

### ✅ 已完全实现并稳定运行 (v1.2.1)
1. **桌面画布与图标管理 (ListView & SelectArea)**
   - **分层与渲染**：`LinDesktop`（桌面宿主背景）→ `LinWidgetCanvasWidget`（CEF OSR 画布）→ `ListView`（透明图标与网格层）→ `SelectArea`（顶层框选）；
   - **布局与对齐**：支持网格对齐、自由摆放模式、自动分类排序、多档图标视图尺寸（小/中/大/特大），DPI 缩放自动重映射；
   - **自然排序与覆盖**：支持中文拼音、数字自然排序（`file1, file2, file10`）；支持系统与特定云盘目录的显示名本地化覆盖（如 WPS 云盘）；
   - **图标缓存与净化**：`IconCacheManager` 实现 O(1) LRU 淘汰机制（默认容量上限 4096），并在文件增删改时主动失效；UWP / MSIX 微软应用商店图标完全兼容解析；文件夹纯色 matte 底图按 key 缓存净化；
   - **交互性能**：拖拽指示线局部区域刷新，多选拖拽幽灵图（Ghost Preview）动态尺寸限制；120ms 文件系统变动防抖合并节流；桌面图标 renderData 仅在状态变更时重算。
2. **桌面收纳盒 (LinBox)**
   - **容器管理**：自定义收纳盒名称、主题颜色、透明度、折叠/展开、图标/列表双视图切换；
   - **文件夹映射**：支持将物理文件夹映射为收纳盒，基于 `SHChangeNotify` 与目录监听实现增删改查实时双向同步；
   - **数据持久化安全**：专有二进制混淆与压缩（`LDBX` 魔数 + 异或掩码 + `qCompress`），损坏数据自动重命名隔离为 `.corrupted_yyyyMMdd_hhmmss` 保留现场；Debug 模式自动伴随生成 `data.dat.debug.json` 明文对照。
3. **HTML5 桌面小组件平台 (LinWidget System)**
   - **独立运行时与动态加载**：核心功能剥离为独立的 `LinWidgetRuntime.dll`，主程序 `LinDesk.exe` 零静态 CEF 链接，支持无组件裸跑或按需安装/卸载；
   - **CEF OSR 离屏合成**：彻底移除 WebView2，基于 Chromium Embedded Framework 实现每组件独立 OSR 离屏渲染实例，通过 `QPainter` 统一绘制管线直接将像素帧合成到底层，无任何子窗口 HWND 遮挡与黑边闪烁；
   - **双向通信协议**：支持原生 Bridge 拦截通道与兼容标准 `postMessage`，提供 `storage`（隔离持久化存储）、`systemInfo`（CPU/GPU/内存实时监控快照与推送）、`instance`（多实例管理）等能力，内置 `window.chrome.webview` 兼容垫片；
   - **丰富内置组件**：内置 Clock（极简时钟）、SysMonitor（系统监控）、Weather（天气）、Notes（便签）、Timer（计时器）、Todo（待办清单）、WaterTracker（饮水提醒），以及 **Wallpaper（全屏动态壁纸组件）**；
   - **开发模式与热重载**：组件支持目录监听热刷新与工程一键打包安装。
4. **系统级集成与扩展**
   - **Win11 原生右键菜单适配**：通过 `Win11MenuIsland` 独立宿主进程与 `Win11MenuBridge` 命名管道实现现代 Win11 桌面右键菜单原生呈现，无缝衔接 Shell New 与第三方菜单项；
   - **OCR 插件能力**：基于 Qt 插件架构的 `Plugins/OCRModule`，支持截屏选区、后台异步文字识别与结果复制；
   - **AI 大模型服务**：支持多 Provider（OpenAI / Claude / Google / Ollama）的后台异步工作线程与流式交互（`AICoreService`）；
   - **日志与崩溃转储**：基于 `spdlog` 的滚动多文件日志系统（落盘 `%LOCALAPPDATA%/LinDesk/logs/`），以及 MiniDump 崩溃捕获机制；
   - **双架构交付与自动更新**：支持 Windows x64 与 x86 双架构 CMake 编译构建，Inno Setup 参数化打包，以及 GitHub Release 自动化发布脚本。
5. **单元测试与质量保障**
   - 包含 8 个独立 GoogleTest 测试套件，覆盖系统信息、协议信封、日志器、持久化格式、运行时桥、运行时安装器、商店图标兼容与显示名覆盖，测试通过率 100%。
6. **桌面人机工学交互与 High-DPI 全面适配**
   - **LinBox 动效与吸附**：平滑弹性高度缓动折叠（200ms `OutCubic`）、标题栏四分区双击命中（文字重命名 / 空白折叠 / 图标打开资源管理器）、鼠标悬停自动下卷展开（Auto-Peek Roll-up）、12px 屏幕边缘与相邻盒子 0 间隙磁吸（按住 `Alt` 自由微调）；
   - **原生键盘直觉**：盲打首字母跳选定位（Type-to-Select，800ms 缓冲区，全字串与拼音首字母检索，支持盒子内跳选与平滑居中滚动）；全局撤销快捷键（`Ctrl + Z` 桥接 Windows Explorer Shell Undo 栈）；
   - **双击隐藏桌面动效**：双击桌面空白处触发 220ms `InOutQuad` 透明度平滑淡入淡出过渡，设置中心与右键菜单提供独立双向同步开关；
   - **High-DPI 缩放与防模糊**：全链路采用 `scaledPx()` 缩放，完美适配 125%/150%/175%/200% 等非 100% 缩放比率；`TextRenderCache` 动态绑定 `devicePixelRatio` 消除文字毛刺发虚；4K 壁纸物理分辨率同步解码；毛玻璃模糊半径自适应 DPI；
   - **基础性能大重构**：多图标拖拽单层幽灵图合并渲染（$O(N) \to O(1)$ 开销）；纯 QPainter 直绘框选矩形与联合脏矩形局部刷新（零残影）；QuadTree 空间四叉树裁剪削减 70%~90% 冗余重绘开销；两级 LRU 图标缓存与渐进式异步加载。

---

### ⏳ 进行中与规划待办 (Roadmap & Todo)
1. **桌面智能收纳与进阶交互（P2）**：
   - **一键智能分类整理**：自动识别并归集快捷方式、常用文档、压缩包、文件夹到对应标准 Box，保留固定项，支持新增文件自动归集规则；
   - **拖拽进入 Box 投掷反馈**：文件拖向某个 Box 时提供克制的主题色边框高亮反馈。
2. **多屏幕扩展与能耗优化（P2/P3）**：
   - **多显示器跨屏几何**：破除单屏绑定，支持多屏 Virtual Geometry 跨屏自由摆放 Box；
   - **全屏独占免打扰休眠**：独占全屏 3D 游戏 / PPT 放映时自动挂起组件渲染，实现 CPU/GPU 0.0% 零抢占。
3. **小组件生态进阶（P1/P2）**：
   - 天气小组件离线与超时持续重试机制（避免 3 次失败后停止刷新）；
   - 便签组件多实例设置页统一维护管理；
   - LinWidget 进程权限细粒度沙箱隔离与崩溃自动恢复心跳。
4. **渲染加速（P2/P3）**：
   - 基于 OpenGL / DirectComposition 的全桌面 GPU 硬件加速管线实现（当前已有开关入口）。
5. **剪贴板工具增强（P3）**：
   - 剪贴板历史记录、监控与快速贴附工具。

---

## 🛠️ 技术栈与架构全景

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        LinDesk 主程序 (LinDesk.exe)                    │
│                                                                        │
│   ┌─────────────────────┐   ┌──────────────────────────────────────┐   │
│   │     桌面底座宿主     │   │          统一 QPainter 绘制管线       │   │
│   │     LinDesktop      │───│  1. 静态壁纸 / 全屏壁纸组件 (底层)     │   │
│   │ (Win32 WorkerW 挂载)│   │  2. LinWidgetCanvasWidget (中层OSR)  │   │
│   └─────────────────────┘   │  3. ListView 图标层 & LinBox (顶层)  │   │
│              │              └──────────────────────────────────────┘   │
│              ▼                                  ▲                      │
│   ┌─────────────────────┐                       │                      │
│   │ LinWidgetRuntimeBridge ◄────────────────────┘                      │
│   │  (动态加载导出函数桥) │                                              │
│   └──────────┬──────────┘                                              │
└──────────────┼─────────────────────────────────────────────────────────┘
               │ LoadLibraryW 动态加载
               ▼
┌────────────────────────────────────────────────────────────────────────┐
│             组件系统运行时动态库 (LinWidgetRuntime.dll)                │
│                                                                        │
│  ┌──────────────────────┐  ┌────────────────────────────────────────┐  │
│  │   CefManager 单例    │  │       CefOsrBrowserInstance (每组件)   │  │
│  │ (100Hz 协同消息循环)  │  │   - Chromium OSR 离屏渲染              │  │
│  └──────────────────────┘   │   - BGRA 帧缓冲输出                    │  │
│              │              │   - 双向 JSON IPC 协议 & 沙箱权限控制    │  │
│              ▼              └───────────────────▲────────────────────┘  │
│  ┌──────────────────────┐                       │                      │
│  │  LinDeskSubprocess   │◄──────────────────────┘                      │
│  │   (CEF 独立渲染子进程) │                                              │
│  └──────────────────────┘                                              │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 🚀 编译与开发指南 (Build & Setup)

### 环境要求
- **操作系统**：Windows 10 / Windows 11 (x64 或 x86)
- **编译工具**：Visual Studio 2022 (MSVC v143, 需勾选 C++20 与 MFC/ATL 支持)
- **构建系统**：CMake 3.31 或更高版本
- **包管理器**：vcpkg (启用 Manifest 模式)
- **Qt 版本**：Qt 5.15.2 (MSVC 2019/2022 64-bit 或 32-bit)
- **CEF SDK**：Chromium Embedded Framework 120+ (放置于 `3rdparty/cef`，可运行配套脚本自动获取)

### 1. 安装第三方依赖 (vcpkg)
```powershell
# 在 LinDesk 根目录下执行
vcpkg install --triplet x64-windows
```

### 2. 获取 CEF SDK
LinDesk 提供便捷的下载校验脚本：
```powershell
python scripts/download_cef.py
```
脚本会自动下载匹配的 CEF 二进制包并解压至 `3rdparty/cef`。

### 3. 生成 CMake 构建工程
```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

### 4. 编译与运行测试
```powershell
# 编译整个解决方案
cmake --build build --config Debug

# 运行全套自动化测试
ctest --test-dir build -C Debug --output-on-failure
```

---

## 📂 项目模块结构 (Source Tree)

```text
LinDesk/
├── CMakeLists.txt              # CMake 根构建脚本（版本号单一来源、目标定义）
├── CMakePresets.json           # 推荐构建预设
├── vcpkg.json                  # vcpkg 依赖声明 (spdlog, gtest 等)
├── Readme.md                   # 本说明文件
├── agent.md                    # AI 编码约束与规范（UTF-8、spdlog 规则等）
│
├── src/                        # 主程序源码目录
│   ├── app/                    # 应用程序入口、主窗口、Version.h、资源与字体
│   ├── desktop/                # 桌面核心：ListView、SelectArea、RightMenu、Win11MenuBridge
│   │   ├── runtime/            # LinWidget 运行时安装/卸载/探测管理器 (RuntimeInstaller)
│   │   └── widgets/            # 小组件平台核心
│   │       ├── cef/            # CEF OSR 离屏渲染封装与子进程 (LinDeskSubprocess)
│   │       ├── linwidget/      # 组件包解析、布局、注册表、权限与 IPC 消息协议
│   │       └── runtime_export.*# LinWidgetRuntime.dll 动态链接与导出桥
│   ├── box/                    # LinBox 桌面收纳盒与 BoxManager
│   ├── monitor/                # 文件系统监听 (SHChangeNotify)、注册表与配置监控
│   ├── services/               # 系统集成、图标提取与 LRU 缓存、持久化、更新与崩溃日志
│   ├── models/                 # 数据模型、渲染数据与四叉树 (QuadTree) 空间索引
│   ├── plugins/                # 插件基接口 (ILinPlugin / IOCRPlugin) 与插件加载器
│   ├── ui/                     # Fluent 风格通用组件库与系统设置界面
│   ├── ai/                     # AI 核心服务与多大模型客户端
│   └── resources/              # 应用图标、样式表与 11 个内置小组件包 (LinWidgets/)
│
├── docs/                       # 官方文档中心 (自包含 HTML & Markdown)
│   ├── LinDesk_Docs_Index.html          # 文档中心总览与阅读导航
│   ├── LinDesk_Product_Intro.html       # 详细产品定位与功能手册
│   ├── LinDesk_Module_Guide.html        # 各模块源码职责、依赖与构建矩阵
│   ├── LinDesk_Architecture_Guide.html  # 核心架构指南（统一绘制管线与 CEF 运行时）
│   ├── LinDesk_Todo.html                # 全量任务清单与进度跟踪
│   ├── LinDesk_Widget_Platform_Plan.html# 组件系统设计方案与任务拆解
│   ├── LinDesk_Widget_Management_Interaction_Plan.html # 组件管理与交互方案
│   └── LinWidget_Protocol.html          # LinWidget 协议与组件开发权威规范
│
├── Plugins/OCRModule/          # OCR 截图文字识别动态库插件工程
├── W11MenuUI/                  # Win11 原生菜单宿主 (Win11MenuIsland.vcxproj)
├── LinDeskUpdate/              # 独立更新下载器子工程
├── installer/                  # Inno Setup 打包脚本与更新元数据配置
├── scripts/                    # 构建、CEF 下载、多语言提取与 Release 发布脚本
├── tests/                      # 8 个自动化 GTest 单元测试工程
└── translations/               # 多语言本地化源文件 (zh_CN, en_US)
```

---

## 📚 文档中心快速入口 (Documentation Navigation)

建议使用浏览器直接打开 `docs/` 下的 HTML 文档，获得交互式导航与完整的图文排版：

| 文档名称 | 核心内容 | 目标读者 |
| :--- | :--- | :--- |
| **[文档中心 (Index)](docs/LinDesk_Docs_Index.html)** | 全局文档体系总索引、推荐阅读路径 | 所有人 |
| **[产品介绍 (Product Intro)](docs/LinDesk_Product_Intro.html)** | 产品定位、设计哲学、功能清单、技术栈 | 用户 / 产品 / 开发者 |
| **[模块介绍 (Module Guide)](docs/LinDesk_Module_Guide.html)** | 源码模块细化解析、依赖拓扑、测试矩阵 | 核心开发者 |
| **[架构指南 (Architecture Guide)](docs/LinDesk_Architecture_Guide.html)** | CEF OSR 离屏渲染管线、核心人机工学交互、High-DPI 缩放、持久化 | 架构师 / 开发者 |
| **[任务清单 (Todo List)](docs/LinDesk_Todo.html)** | 全局单一真实来源：已完成里程碑与待办事项对照表 | 开发者 / 测试 |
| **[组件方案 (Widget Platform)](docs/LinDesk_Widget_Platform_Plan.html)** | LinWidget 架构演进、运行时安装/卸载与路线图 | 组件与平台开发者 |
| **[组件管理与交互 (Widget UX)](docs/LinDesk_Widget_Management_Interaction_Plan.html)** | 组件管理 HTML/SPA 化、消灭壁纸框选冲突、智能缩放手柄 | 开发者 / 架构师 |
| **[组件协议与规范 (LinWidget Protocol)](docs/LinWidget_Protocol.html)** | HTML5 小组件 Manifest 规范、Bridge 双向 IPC 与 API 字典 | 小组件开发者 / AI Agent |

---

## 🛡️ 编码约束与许可 (Agent Rules & License)

所有源文件（`.cpp / .h / .json / .html / .md`）一律遵循 **UTF-8（无 BOM）** 编码。日志必须统一收口于 `LogManager`（基于 `spdlog`），禁止裸写 `std::cout` 或 `printf`。详细规范请参阅 [`agent.md`](agent.md)。

本项目采用 **[GNU General Public License v3.0 (GPLv3)](LICENSE)** 开源许可证。欢迎提交 Issue 与 Pull Request 共同建设！
