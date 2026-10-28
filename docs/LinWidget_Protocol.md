# LinWidget 协议与组件开发规范 (LinWidget Protocol & Authoring Specification)

> **版本**：1.0.0  
> **适用环境**：LinDesk Desktop Platform (Qt 5.15+ / CEF OSR Windowless)  
> **文档定位**：面向组件开发者与 AI 编程代理的官方权威协议与交互规范。

---

## 1. 架构总览 (Architecture Overview)

LinDesk 组件系统采用 **单进程 · 动态加载 · 离线画布合成 (CEF OSR)** 架构：
- **主程序 (`LinDesk.exe`)**：零静态 CEF 链接，仅在需要时通过动态库桥接接口 (`LinWidgetRuntimeBridge`) 加载 `LinWidgetRuntime.dll`。
- **组件运行时 (`LinWidgetRuntime.dll`)**：负责宿主 Chromium Embedded Framework (CEF)、管理 OSR 浏览器离线渲染实例、执行安全沙箱策略并处理双向 IPC 消息。
- **桌面画布合成 (`LinWidgetCanvasWidget`)**：将各组件实例与动态壁纸的 BGRA 渲染帧，直接绘制到 Windows 桌面图层画布，与原生图标完美混流。
- **数据隔离约束**：组件系统只读写 `LinWidgets/`、`LinWidgetsData/` 与 `LinWallpapers/`，严禁触碰或修改主桌面持久化数据文件 (`data.dat` / `data.json`)。

---

## 2. 组件包规范与 Manifest 规范 (Package & Manifest)

每个组件为一个独立文件夹（或 `.zip` 包），包根目录必须包含 `manifest.json`：

### 2.1 目录结构
```text
MyWidget/
├── manifest.json       # 组件元数据与权限声明（必须）
├── icon.png            # 组件图标（推荐 128x128 PNG）
├── index.html          # 入口 HTML 页面
├── style.css           # 样式表
└── script.js           # 逻辑脚本
```

### 2.2 `manifest.json` 字段规范
```json
{
  "id": "com.example.clock",
  "name": "极简时钟",
  "version": "1.0.0",
  "author": "LinDesk Community",
  "description": "半透明毛玻璃桌面时钟组件",
  "icon": "icon.png",
  "role": "widget",
  "type": "html",
  "entry": "index.html",
  "width": 320,
  "height": 140,
  "minWidth": 160,
  "minHeight": 100,
  "maxWidth": 640,
  "maxHeight": 480,
  "transparent": true,
  "resizable": true,
  "autoLoad": true,
  "multiInstance": false,
  "permissions": [
    "storage",
    "desktop"
  ],
  "intent": {
    "prompt": "创建一个带有暗黑毛玻璃质感、秒针呼吸灯效果的桌面时钟",
    "ai_engine": "Claude-3.7-Sonnet",
    "created_at": "2026-10-14T01:30:00Z"
  }
}
```

#### 核心字段说明：
| 字段 | 类型 | 必填 | 默认值 | 说明 |
| :--- | :--- | :---: | :---: | :--- |
| `id` | `string` | 是 | - | 唯一标识符，小写字母、数字及点号（如 `lindesk.clock`） |
| `name` | `string` | 是 | - | 组件显示名称 |
| `version` | `string` | 是 | `1.0.0` | 语义化版本号（`X.Y.Z`） |
| `role` | `string` | 否 | `"widget"` | 组件角色：`"widget"`（普通桌面组件）或 `"wallpaper"`（全屏动态壁纸） |
| `entry` | `string` | 否 | `"index.html"` | 包内入口页面相对路径 |
| `width` / `height` | `number` | 是 | - | 初始宽度与高度（像素，仅 `role="widget"` 生效） |
| `role="wallpaper"` | - | - | - | 壁纸组件视口尺寸自动跟随桌面分辨率，点击默认全穿透 |
| `transparent` | `boolean` | 否 | `true` | 是否支持 Alpha 透明通道渲染 |
| `resizable` | `boolean` | 否 | `true` | 是否允许用户拖拽边角缩放尺寸 |
| `permissions` | `array` | 否 | `[]` | 申请的特权数组：`"storage"`, `"desktop"`, `"network"` |
| `intent` | `object` | 否 | `{}` | 记录 AI 辅助生成的原始需求与引擎信息，供增量修复参考 |

---

## 3. 双向通信桥接机制 (Bridge Protocol)

组件运行在独立的 Chromium 隔离沙箱环境中，通过统一的 Bridge 协议与 C++ 宿主双向通信。

### 3.1 组件向宿主发送请求 (Outbound)
组件支持两种等价的消息发送通道：

#### 推荐方式 A：原生 Bridge 拦截通道 (零开销)
```javascript
function sendHostMessage(message) {
  fetch('http://linwidget.bridge/postMessage', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(message)
  }).catch(function (err) {
    console.error('Bridge postMessage error:', err);
  });
}
```

#### 兼容方式 B：Standard postMessage
```javascript
window.parent.postMessage(message, '*');
```

### 3.2 宿主向组件推送消息/响应 (Inbound)
组件在页面中监听 `window` 的 `message` 事件即可接收响应：
```javascript
window.addEventListener('message', function (event) {
  const msg = event.data;
  if (!msg || typeof msg !== 'object') return;
  console.log('Received from host:', msg.type, msg);
});
```

---

## 4. 标准协议消息面字典 (Message Schema Reference)

### 4.1 系统信息 API (`SystemInfo`)
需要权限：`desktop`

#### 4.1.1 获取即时快照 (`system_info_snapshot` / `systemInfoRequest`)
- **请求消息**：
```json
{
  "type": "system_info_snapshot",
  "requestId": "req_101",
  "action": "snapshot"
}
```
> `action` 可选值：`"snapshot"`（全量）、`"cpu"`、`"memory"`、`"gpu"`、`"disk"`、`"network"`、`"battery"`、`"os"`、`"uptime"`。

- **宿主响应** (`systemInfoResponse`)：
```json
{
  "type": "systemInfoResponse",
  "requestId": "req_101",
  "ok": true,
  "data": {
    "cpu": { "usagePercent": 14.5, "coreCount": 16, "brand": "13th Gen Intel Core i7" },
    "memory": { "totalBytes": 34359738368, "usedBytes": 17179869184, "usagePercent": 50.0 },
    "battery": { "hasBattery": true, "percent": 98, "charging": true }
  }
}
```

#### 4.1.2 订阅周期推送 (`system_info_watch`)
- **请求消息**：
```json
{
  "type": "system_info_watch",
  "requestId": "req_102",
  "watchId": "cpu_watcher_1",
  "action": "watch",
  "intervalMs": 1000
}
```
- **宿主推送** (`systemInfoUpdate`)：
```json
{
  "type": "systemInfoUpdate",
  "watchId": "cpu_watcher_1",
  "data": { ... }
}
```

#### 4.1.3 取消订阅 (`system_info_unwatch`)
```json
{
  "type": "system_info_unwatch",
  "requestId": "req_103",
  "watchId": "cpu_watcher_1",
  "action": "unwatch"
}
```

---

### 4.2 本地持久化存储 API (`Storage`)
需要权限：`storage`。数据独立隔离保存在 `<userData>/LinWidgetsData/<widgetId>/storage.json` 中。

#### 4.2.1 存储键值 (`storage_set`)
```json
{
  "type": "storage_set",
  "requestId": "req_201",
  "action": "set",
  "key": "theme_mode",
  "value": "dark"
}
```

#### 4.2.2 读取键值 (`storage_get`)
```json
{
  "type": "storage_get",
  "requestId": "req_202",
  "action": "get",
  "key": "theme_mode"
}
```
- **宿主响应** (`storageResponse`)：
```json
{
  "type": "storageResponse",
  "requestId": "req_202",
  "ok": true,
  "value": "dark"
}
```

#### 4.2.3 删除键值 (`storage_remove`) / 清空 (`storage_clear`)
```json
{ "type": "storage_remove", "requestId": "req_203", "action": "remove", "key": "theme_mode" }
{ "type": "storage_clear", "requestId": "req_204", "action": "clear" }
```

---

### 4.3 动态壁纸专用 API (`Wallpaper`)
仅对 `manifest.role = "wallpaper"` 的组件生效。

#### 4.3.1 弹出文件对话框选择本地媒体 (`wallpaper_set_source`)
- **请求消息**：
```json
{
  "type": "wallpaper_set_source",
  "requestId": "req_301",
  "dialog": true
}
```
- **宿主响应** (`wallpaper_source_changed`)：
```json
{
  "type": "wallpaper_source_changed",
  "requestId": "req_301",
  "ok": true,
  "sourcePath": "D:/Wallpapers/cyberpunk.mp4"
}
```
> **媒体加载规范**：选定本地媒体文件后，通过 `linwidget://wallpaper:<id>/_media?path=` URL 协议直接注入 `<video src="...">` 或 `<img src="...">` 标签，突破 Chromium 本地文件跨域拦截。

#### 4.3.2 恢复读取壁纸媒体源 (`wallpaper_get_source`)
- **请求消息**：
```json
{
  "type": "wallpaper_get_source",
  "requestId": "req_302"
}
```
- **宿主响应** (`wallpaper_source_response`)：
```json
{
  "type": "wallpaper_source_response",
  "requestId": "req_302",
  "ok": true,
  "sourcePath": "D:/Wallpapers/cyberpunk.mp4"
}
```

---

### 4.4 窗口与视口控制 API (`Window & Geometry`)

#### 4.4.1 组件主动调整大小与位置 (`widget_geometry_changed`)
```json
{
  "type": "widget_geometry_changed",
  "x": 100,
  "y": 120,
  "width": 360,
  "height": 200
}
```

#### 4.4.2 请求打开宿主组件设置面板 (`open_widget_settings`)
```json
{
  "type": "open_widget_settings"
}
```

---

## 5. 权限沙箱与安全模型 (Security & Sandbox)

1. **资源协议隔离 (`linwidget://`)**：
   - 所有组件文件必须通过 `linwidget://<instanceId>/<relativePath>` 加载；
   - 宿主严密校验目录穿越攻击（如 `../../`），任何试图跳出组件包目录的请求均被立即阻断（HTTP 404/Block）。
2. **外部网络权限沙箱 (`network`)**：
   - 未在 `manifest.permissions` 中声明 `"network"` 的组件，所有外发网络请求（`http://`, `https://`, `ws://`, `wss://`, `ftp://`）在 CEF `OnBeforeResourceLoad` 层强制拦截返回 `RV_CANCEL`。
3. **输入穿透与透明度检测**：
   - 点击组件上像素 Alpha < 10 的透明区域时，事件自动穿透给桌面原生图标，保证桌面原生操作顺畅。

---

## 6. 开发者工具与 AI 增量修复闭环 (Diagnostic & AI Loop)

调试面板内置 **「导出诊断包」** 功能（快捷键或调试面板按钮）：
1. **脱敏保护**：自动将用户路径（如 `C:\Users\username\`）脱敏替换为 `<USER_HOME>`。
2. **诊断包结构**：
   - 组件完整 Manifest（含意图记录）；
   - 当前运行时状态（尺寸、Z 序、已授予权限）；
   - 最近控制台输出与异常栈（最后 200 行）；
   - 针对 AI 的增量修复指引。
3. **闭环修复模式**：组件发生异常时，直接将导出的诊断包复制给 AI，即可在不推倒重来的前提下针对性修复 Bug。
