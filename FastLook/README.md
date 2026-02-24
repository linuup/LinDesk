# FastLook

> 一个 Windows 文件快速预览工具。在资源管理器或桌面中将鼠标悬停在文件/文件夹上，按下**空格键**即可弹出预览窗口。

---

## 支持的预览类型

| 类型 | 扩展名 |
|------|--------|
| 文本 / 代码 | `.txt` `.py` `.js` `.ts` `.html` `.css` `.json` `.yaml` `.md` 等 |
| 图片 | `.png` `.jpg` `.jpeg` `.gif` `.bmp` `.ico` `.webp` `.svg` |
| PDF | `.pdf` |
| Office 文档 | `.doc` `.docx` `.xls` `.xlsx` `.ppt` `.pptx` |
| 压缩包 | `.zip` `.tar` `.gz` `.bz2` `.xz` `.tgz` `.7z` `.rar` |
| 文件夹 | 目录树浏览，支持搜索 |

---

## 环境要求

- **Python** 3.10+
- **Windows** 10 / 11

---

## 安装依赖

```powershell
pip install -r requirements.txt
```

> **注意：** `.rar` 格式还需在系统 `PATH` 中安装 [unrar.exe](https://www.rarlab.com/rar_add.htm)。

---

## 直接运行（开发模式）

```powershell
python main.py
```

运行后程序在后台监听空格键，悬停文件后按空格即可预览。

---

## 构建为可执行文件

提供两种打包方案，选择其一即可。

### 方案一：PyInstaller（推荐，兼容性好）

**前置安装：**

```powershell
pip install pyinstaller
```

**执行构建：**

```powershell
.\build_pyinstaller.ps1
```

**输出：**

```
FastLook\build-pyinstaller\FastLook.exe
```

---

### 方案二：Nuitka（体积更小，启动更快）

**前置安装：**

```powershell
pip install nuitka
# 需要 C 编译器，推荐安装 MinGW-w64 或 MSVC
```

**执行构建：**

```powershell
.\build_nuitka.ps1
```

**输出：**

```
FastLook\build-nuitka\FastLook.exe
```

> Nuitka 首次构建会自动下载所需的编译依赖（`--assume-yes-for-downloads`），耗时较长，请耐心等待。

---

## 项目结构

```
FastLook/
├── main.py                  # 入口：热键监听 & 窗口控制器
├── preview_manager.py       # 预览窗口（标题栏、置顶、拖拽）
├── explorer_integration.py  # 获取鼠标下的文件路径
├── file_type_detector.py    # 文件类型识别
├── viewers/
│   ├── text_viewer.py       # 代码/文本（Pygments 高亮）
│   ├── image_viewer.py      # 图片
│   ├── pdf_viewer.py        # PDF & Office
│   ├── markdown_viewer.py   # Markdown
│   ├── folder_viewer.py     # 文件夹树形浏览
│   ├── archive_viewer.py    # 压缩包内容浏览
│   └── web_viewer.py        # 通用网页回退
├── style.css                # 全局 UI 样式
├── app_icon.png             # 应用图标
├── requirements.txt         # Python 依赖清单
├── build_pyinstaller.ps1    # PyInstaller 构建脚本
└── build_nuitka.ps1         # Nuitka 构建脚本
```

---

## 使用说明

| 操作 | 效果 |
|------|------|
| 悬停文件/文件夹 + **Space** | 打开预览窗口 |
| 空白处按 **Space** | 关闭所有未固定的预览窗口 |
| 点击 **📌** | 固定窗口（始终置顶，不被关闭） |
| 在文件夹/压缩包预览内悬停条目 + **Space** | 打开该条目的新预览窗口 |
| 拖拽标题栏 | 移动窗口 |
