# LinDesk 小组件极简开发指南 (Widgets Guide)

> **核心原则**：小组件本质上就是**普通的本地 HTML 静态网页卡片**。  
> **不要开启 Visual Studio，不需要编译任何 C++ 代码**。直接用 Chrome 或 VS Code Live Server 打开即可开发与调试！
> *(注：动态壁纸请移步根目录的 `wallpapers/` 专属目录)*

---

## 1. 目录结构与极简约定

每个小组件都是一个独立的文件夹，必须包含两个核心文件：
```text
my-widget/
├── manifest.json      # 组件配置文件（声明名称、默认尺寸、权限）
├── index.html         # 核心网页入口（HTML5 / CSS / JS / Canvas）
├── style.css          # 样式文件（可选）
└── icon.png           # 桌面与设置面板展示的图标（建议 128×128 PNG）
```

---

## 2. 极简 `manifest.json` 模板

复制以下内容保存为 `manifest.json` 即可：

```json
{
  "id": "com.myname.mywidget",
  "name": "我的便签",
  "version": "1.0.0",
  "description": "优雅简约的桌面小组件",
  "entry": "index.html",
  "icon": "icon.png",
  "role": "widget",
  "width": 360,
  "height": 200,
  "minWidth": 200,
  "minHeight": 120,
  "transparent": true,
  "resizable": true
}
```

> **字段说明**：
> - `transparent`: 设为 `true` 开启背景透明（毛玻璃与自适应桌面必备）。
> - `resizable`: 设为 `true` 允许用户在桌面上按住 `Ctrl` 拖拽边缘或滚轮缩放。

---

## 3. 宿主 API（只保留最核心的 3 个）

在 LinDesk 桌面环境下运行时，宿主会在全局注入轻量的 `window.desktop` 对象：

### ① 本地数据持久化 (`storage`)
组件重启后数据不丢失，自动按 `id` 独立存储在各组件专属目录中：
```javascript
// 保存数据
await window.desktop.storage.set("myKey", { theme: "dark", count: 42 });

// 读取数据
const data = await window.desktop.storage.get("myKey");

// 删除数据
await window.desktop.storage.remove("myKey");
```
*(注：在普通浏览器直接打开时，若没有 `window.desktop`，可直接使用原生 `localStorage`。)*

### ② 系统硬件监控 (`systemInfo`)
仅系统监视器、硬件仪表盘等组件使用：
```javascript
const info = await window.desktop.systemInfo.getSnapshot();
console.log("CPU 占用:", info.cpu.usagePercent);
console.log("内存占用:", info.memory.usedPercent);
```

### ③ 打开组件设置 (`openSettings`)
```javascript
window.desktop.openSettings();
```

---

## 4. 浏览器秒级调试工作流 (Zero-C++ Workflow)

1. 双击当前目录下的 **`preview.html`**，直接在浏览器中打开多组件调试沙箱；
2. 可以在沙箱中切换任意小组件，内置虚拟的 `window.desktop` 数据模拟器（自动模拟 CPU 波动与存储读写）；
3. 打开浏览器的 F12 开发者工具，随意修改 HTML/CSS，按 F5 秒级刷新看效果；
4. 调试满意后，把组件文件夹直接拷贝至 `%APPDATA%/LinDesk/LinWidgets/`，重新启动 LinDesk 即可常驻桌面！
