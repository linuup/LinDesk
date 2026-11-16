# LinDesk 小组件极简开发指南

小组件本质上是本地静态网页卡片。无需开启 Visual Studio，不需要编译任何 C++ 代码，直接使用 Chrome 浏览器或轻量本地 Web 服务器即可完成开发与调试。

动态壁纸工程请移步根目录的 wallpapers 专属目录。

---

## 1. 目录结构与极简约定

每个小组件都是独立的文件夹，必须包含两个核心文件：
```text
my-widget/
├── manifest.json      # 组件配置文件，声明名称、默认尺寸与权限
├── index.html         # 核心网页入口，基于 HTML5、CSS、JS 与 Canvas
├── style.css          # 样式文件，可选
└── icon.png           # 桌面与设置面板展示的图标，建议 128×128 像素 PNG
```

---

## 2. 极简 manifest.json 模板

复制以下内容保存为 manifest.json 即可：

```json
{
  "id": "com.myname.mywidget",
  "name": "我的便签",
  "version": "1.0.0",
  "description": "优雅简约的桌面小组件",
  "entry": "index.html",
  "icon": "icon.png",
  "role": "widget",
  "width": 320,
  "height": 420,
  "minWidth": 200,
  "minHeight": 120,
  "transparent": true,
  "resizable": true
}
```

字段说明：
- transparent：设为 true 开启背景透明，毛玻璃与自适应桌面背景必备。
- resizable：设为 true 允许用户在桌面上拖拽边缘调整尺寸或缩放。

---

## 3. 宿主 API

在 LinDesk 桌面环境下运行时，宿主会在全局注入轻量的 window.desktop 对象：

### 本地数据持久化 storage
组件重启后数据不丢失，自动按 id 独立存储在各组件专属目录中：
```javascript
// 保存数据
await window.desktop.storage.set("myKey", { theme: "dark", count: 42 });

// 读取数据
const data = await window.desktop.storage.get("myKey");

// 删除数据
await window.desktop.storage.remove("myKey");
```

在普通浏览器直接打开时，若没有 window.desktop，代码会自动回退到浏览器原生 localStorage。

### 系统硬件监控 systemInfo
仅系统监视器、硬件仪表盘等组件使用，支持一次性快照与连续推送订阅：
```javascript
// 方式 1：获取单次硬件快照
const info = await window.desktop.systemInfo.getSnapshot();
console.log("CPU 占用率:", info.cpu.usagePercent);

// 方式 2：注册周期监听，默认采样间隔 1000 毫秒
const watchId = window.desktop.systemInfo.watch((info) => {
  console.log("实时主频:", info.cpu.currentClockGhz);
  console.log("显卡负载:", info.gpu.usagePercent);
}, 1000);

// 取消监听
await window.desktop.systemInfo.unwatch(watchId);
```

### 打开组件设置 openSettings
```javascript
window.desktop.openSettings();
```

---

## 4. 调试与部署流程

1. 打开浏览器的开发者工具，直接在浏览器中双击组件的 index.html 文件调试；
2. 调试满意后，把组件文件夹直接拷贝至用户配置目录即可常驻桌面；
3. 支持通过 LinDesk 桌面托盘重新加载小组件。
