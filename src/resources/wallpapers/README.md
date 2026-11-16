# LinDesk 动态壁纸极简开发指南 (Live Wallpapers Guide)

> **核心原则**：动态壁纸本质上就是**铺满全屏背景的 HTML5 / Canvas / WebGL 网页**。  
> **不要开启 Visual Studio，不需要编译任何 C++ 代码**。直接用浏览器或双击 `preview.html` 打开即可开发与调试！

---

## 1. 目录结构与极简约定

每个动态壁纸都是一个独立的文件夹：
```text
my-wallpaper/
├── manifest.json      # 壁纸配置文件（必须 role: "wallpaper"）
├── index.html         # 核心全屏网页（HTML5 / WebGL / Canvas / CSS 动效）
├── style.css          # 样式文件
└── preview.png        # 壁纸预览缩略图（推荐 16:9 比例，如 384×216 PNG）
```

---

## 2. 极简 `manifest.json` 模板

复制以下内容保存为 `manifest.json` 即可：

```json
{
  "id": "com.myname.cosmic-wallpaper",
  "name": "深空粒子",
  "version": "1.0.0",
  "description": "极具深邃质感的暗黑流体粒子壁纸",
  "entry": "index.html",
  "preview": "preview.png",
  "role": "wallpaper",
  "interactive": true
}
```

> **字段说明**：
> - `role`: **必须填 `"wallpaper"`**，宿主引擎会自动将其作为底层全屏壁纸铺展在整个屏幕视口，并置于所有桌面图标和小组件下方；
> - `interactive`: 是否接收鼠标交互事件（设为 `true` 可接收 60fps 鼠标移动坐标实现粒子引力跟随或视差倾斜；设为 `false` 则纯展示动效、彻底不消耗任何交互计算）。

---

## 3. 全屏布局与性能黄金法则

1. **视口完全铺满 (CSS Reset)**：
   ```css
   html, body {
     width: 100vw;
     height: 100vh;
     overflow: hidden;
     margin: 0;
     padding: 0;
     background: #000;
   }
   ```
2. **低功耗与节流 (Low Power Budget)**：
   - 当没有鼠标移动时，动画帧率或粒子运动应保持克制，避免 CPU/GPU 长期满载发热；
   - 避免使用超大体积未压缩视频；背景视频建议使用 H.264 / WebM 编码，分辨率匹配 1080P/2K/4K。
3. **鼠标交互 (Interactive Follow)**：
   - 监听原生的 `window.addEventListener('mousemove', e => { ... })` 即可；
   - 宿主底层自带 60fps 广播管道，桌面图标 Hover 和壁纸粒子流动两不耽误。

---

## 4. 浏览器全屏调试工作流

1. 双击当前目录下的 **`preview.html`**，直接在浏览器中打开全屏壁纸预览沙箱；
2. 可以在沙箱中切换任意动态壁纸，随意移动鼠标测试粒子跟随与视差效果；
3. 打开浏览器的 F12 开发者工具，按 F5 秒级刷新看效果；
4. 调试满意后，把壁纸文件夹直接拷贝至 `%APPDATA%/LinDesk/LinWallpapers/`，重新启动 LinDesk 即可常驻桌面！
