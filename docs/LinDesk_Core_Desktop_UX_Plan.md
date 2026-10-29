# LinDesk 桌面核心功能体验优化方案与路线图

- **文档定位**：LinDesk 桌面核心交互、微交互质感、键盘与鼠标人机工学体验优化方案
- **更新日期**：2026-10-29
- **对标产品**：Stardock Fences 5、腾讯桌面整理、Coodesker、Windows 11 原生桌面交互
- **核心目标**：从“基础功能可用”跃升到“一线商业级丝滑质感与跟手感”，消除机械生硬感与盲区

---

## 1. 背景与现状诊断

LinDesk 目前已完成桌面底座嵌入、壁纸同步渲染、LinBox 容器化、Win11 原生上下文菜单适配与 CEF 小组件运行时的核心技术闭环。冷启动时的壁纸同步加载与首帧同频渲染已彻底消除了启动闪烁。

但在日常高频的“人机操作细节”（交互手感、微动画、键盘习惯、边缘反馈）上，仍存在若干与 Windows 原生习惯或商业标杆软件脱节的痛点：
1. **动画缺失**：盒子折叠展开为瞬间硬切尺寸（`setFixedHeight`），隐藏桌面图标为瞬间瞬隐，缺乏流体感；
2. **对齐费力**：盒子移动没有屏幕边缘和邻近盒子边缘的磁吸（Snap），多盒子并排或贴边需反复像素级微调；
3. **键盘习惯断层**：桌面不支持盲打首字母跳选文件（Type-to-Select），误删/误拖后不支持 `Ctrl + Z` 撤销；
4. **高阶功能缺失**：缺少 Fences 标志性的“折叠盒悬停自动下卷展开 (Roll-up)”与真正实用的“一键桌面智能分类整理”。

---

## 2. 核心功能深度优化方案

### 维度一：桌面盒子 (LinBox) 交互质感与高阶能力

#### 1. 折叠 / 展开的弹性缓动动画 (Smooth Height Animation)
- **痛点**：目前点击右上角折叠按钮时，直接调用 `setFixedHeight()` 强制瞬变尺寸，视觉突兀生硬。
- **方案**：
  - 使用 `QVariantAnimation` 或 `QPropertyAnimation`，设定时长 180ms ~ 220ms，缓动曲线采用 `QEasingCurve::OutCubic`；
  - 展开时：盒子高度自标题栏平滑拉伸至原始高度，内容区域由透明渐显；
  - 收起时：内容区域先淡出，高度平滑收缩至标题栏高度；
  - 代码位置：`src/box/LinBox.cpp` 中的 `toggleCollapse()`。

#### 2. 双击标题栏折叠 与 悬停自动下卷展开 (Roll-up / Auto-Peek)
- **痛点**：目前只能点击右上角约 14px 的细小折叠按钮，操作成本高；桌面折叠后取用文件需要手动展开再手动收回。
- **方案**：
  - **双击折叠**：在 `LinBox::mouseDoubleClickEvent` 中检测是否点击在标题栏非按钮区域，若是则直接触发 `toggleCollapse()`；
  - **悬停自动展开 (Auto-Peek / Roll-up)**：
    - 若盒子处于折叠状态，鼠标悬停在标题栏停留超过 150ms（通过 `QTimer` 防误触），盒子平滑下卷展开供挑选操作文件；
    - 当鼠标离开整个盒子区域且无拖拽操作，延时 250ms 平滑收起回到折叠状态；
  - **达成效果**：用户日常可把所有盒子折叠成一条细长雅致的“标签栏”，桌面整洁到极致，用时鼠标一碰即展开，随用随收。

#### 3. 拖拽移动磁吸与对齐吸附 (Magnetic Snapping)
- **痛点**：拖拽盒子或拉伸尺寸时，完全靠人眼微调，极难与其他盒子水平/垂直对齐，也极难紧贴屏幕右侧或顶部边界。
- **方案**：
  - 在 `LinBox::mouseMoveEvent` 的 `m_isMoving` 分支中引入吸附判定（吸附阈值 `kSnapThreshold = 10px`）：
    - **吸附目标 1**：屏幕有效可用工作区边缘（左、右、顶、任务栏顶部）；
    - **吸附目标 2**：其他可见 `LinBox` 的四边（Left, Right, Top, Bottom）；
  - 当距离小于 10px 时自动吸附对齐，并可选择绘制淡蓝色的对齐辅助虚线；按住 `Alt` 键可临时禁用吸附进行自由微调；
  - 代码位置：`src/box/LinBox.cpp` 移动计算逻辑。

#### 4. 真正的一键智能分类整理 (Smart Auto-Organization)
- **痛点**：右键菜单的“整理桌面”当前只是名称字母排序，新用户建立盒子后必须一个个手动挑文件拖入，初次体验门槛高。
- **方案**：
  - 实现真正的智能整理功能：
    - 自动创建（或匹配已有）4 类标准盒子：`快捷方式`、`文件夹`、`文档与表格`、`压缩包与安装包`；
    - 将散落在桌面上的文件根据扩展名与属性批量迁移至对应盒子中；
    - 对已固化在桌面的重要图标（如“此电脑”、“回收站”）保持原地不动；
  - **规则盒子 (Smart Rules)**：支持对盒子设置规则，如“新下载到桌面的图片自动归档进此盒子”；
  - 代码位置：`src/desktop/ListView_Actions.cpp` 中的 `onMenuOrganizeDesktopByNameSort` 重构升级。

---

### 维度二：桌面图标交互与键盘原生直觉

#### 1. 盲打首字母跳选定位 (Type-to-Select)
- **痛点**：在 Windows 桌面按下按键（如输入 `W`），光标会自动选中 `WeChat`。LinDesk 目前忽略了字符按键，无法键盘快速定位。
- **方案**：
  - 在 `ListView::keyPressEvent` 中维护键入缓冲区与 600ms 定时器：
    - 收集用户快速输入的字符串（如 `"chr"` 或单字母 `"v"`）；
    - 遍历桌面图标的 `displayName`（包括支持拼音首字母匹配），查找匹配项；
    - 命中后直接选中该图标并设置焦点，连续按同字母循环切换下一个匹配项；
  - 代码位置：`src/desktop/ListView.cpp` 的 `keyPressEvent`。

#### 2. 全局撤销快捷键 (`Ctrl + Z`) 支持
- **痛点**：在桌面误删、误拖文件后，按下 `Ctrl + Z` 没有任何反应，缺乏防误触保障。
- **方案**：
  - 在 `ListView::keyPressEvent` 中捕获 `QKeySequence::Undo`（`Ctrl + Z`）；
  - 调用 Windows Shell 的 `CSystemApi::UndoFileOperation()` 或触发 `CLSID_Undo` 操作撤销误删与误移；
  - 代码位置：`src/desktop/ListView.cpp` 与 `src/desktop/ListViewFileOpsHelper.cpp`。

#### 3. 双击空白隐藏桌面的淡入淡出动画 (Fade In/Out Transition)
- **痛点**：目前双击空白处隐藏/展示桌面图标时为瞬切硬变，视觉跳动明显。
- **方案**：
  - 引入 150ms 的 `m_desktopVisibilityOpacityAnimation`；
  - 隐藏时透明度从 `1.0 -> 0.0`，完成后设置组件隐藏；
  - 显示时先唤起组件，透明度从 `0.0 -> 1.0` 平滑淡入；
  - 代码位置：`src/desktop/ListView_Actions.cpp` 的 `onMenuShowDesktopIcons()`。

---

### 维度三：拖拽交互手感与视觉反馈

#### 1. 拖入 Box 的边界呼吸光晕反馈 (Drop Target Accent Glow)
- **痛点**：拖拽文件滑过桌面各个盒子时，仅靠光标旁小文字提示，目标盒子的主体视觉不够明确。
- **方案**：
  - 在 `LinBox::dragEnterEvent` 与 `dragLeaveEvent` 中设置 `m_isDropTargetActive = true/false`；
  - 绘制时在 Box 边框渲染一圈柔和的主题色发光外边框（2px ~ 3px Accent Color Glow）；
  - 代码位置：`src/box/LinBox.cpp` 绘图管线。

#### 2. 多文件拖拽的层叠缩略图与数量角标 (Drag Badge)
- **痛点**：多选文件拖拽时指示不明显，用户无法确信自己到底抓取了几个文件。
- **方案**：
  - 拖拽启动时生成最多 3 张微倾斜的堆叠图标阴影，并在右下角绘制药丸状数字角标（如 `+6`）；
  - 代码位置：`src/desktop/ListViewDragDropHelper.cpp`。

---

### 维度四：多显示器与多屏幕扩展支持

#### 1. 跨屏摆放与虚拟桌面几何支持 (Multi-Monitor Span)
- **痛点**：代码中写死了 `QApplication::primaryScreen()->geometry()`，双屏用户在副屏上无法正常摆放盒子。
- **方案**：
  - 将桌面视口扩展到整个虚拟桌面边界（`QGuiApplication::primaryScreen()->virtualGeometry()`）；
  - `LinDesktop` 与 `ListView` 支持跨屏边界绘制，支持把 Box 拖至副屏并持久化保存副屏坐标；
  - 代码位置：`src/desktop/LinDesktop.cpp` 与 `src/services/DpiScaleHelper.cpp`。

---

### 维度五：性能、空闲功耗与“零打扰”静默体验

#### 1. 全屏独占游戏 / 演示免打扰休眠模式 (Game & Presentation Mode)
- **痛点**：用户全屏打 3D 游戏或放映 PPT 时，后台的壁纸监听、CEF 离屏小组件重绘可能无谓消耗 CPU/GPU 算力。
- **方案**：
  - 检测前台是否有独占全屏窗口（`SHAppBarMessage` 或 `GetForegroundWindow` + 屏幕分辨率比对）；
  - 处于全屏独占时，LinDesk 自动挂起一切桌面小组件刷新和定时器，CPU/GPU 占用归 0.0%；退出全屏后立即无缝恢复。

---

## 3. 落地路线图与开发优先级

| 阶段 | 事项 | 影响模块 | 研发预估 | 客户体验提升度 |
| :--- | :--- | :--- | :--- | :--- |
| **P0（最高优）** | **LinBox 折叠/展开平滑高度缓动动画** | `LinBox.cpp` | 0.5 天 | ★★★★★（消除生硬跳变，质感飞跃） |
| **P0** | **双击标题栏折叠 + 悬停自动展开 (Roll-up)** | `LinBox.cpp` | 0.5 天 | ★★★★★（桌面极致整洁的杀手级功能） |
| **P0** | **移动盒子磁吸屏幕与邻近盒子对齐 (Snap)** | `LinBox.cpp` | 1 天 | ★★★★★（解决强迫症对不齐的痛苦） |
| **P1** | **键盘盲打首字母跳选定位 (Type-to-Select)** | `ListView.cpp` | 0.5 天 | ★★★★☆（对齐 Windows 原生习惯，键盘党福音） |
| **P1** | **桌面全局撤销快捷键 (`Ctrl + Z`)** | `ListView.cpp` | 0.5 天 | ★★★★☆（误删误拖的安全感兜底） |
| **P1** | **双击空白处隐藏桌面的淡入淡出动画** | `ListView_Actions.cpp` | 0.5 天 | ★★★★☆（看壁纸更加赏心悦目） |
| **P2** | **真正的一键桌面智能整理分类** | `ListView_Actions.cpp` | 1.5 天 | ★★★★★（新用户安装后的第一惊艳爽点） |
| **P2** | **拖拽进入 Box 的呼吸发光高亮提示** | `LinBox.cpp` | 0.5 天 | ★★★☆☆（投掷反馈更清晰） |
| **P3** | **多屏幕跨屏摆放 Box 支持** | `LinDesktop.cpp` | 2 天 | ★★★★☆（拿下双屏重度办公/开发群体） |
| **P3** | **全屏游戏/PPT 免打扰休眠** | `LinDesktop.cpp` | 1 天 | ★★★☆☆（极致省电，零抢资源） |
