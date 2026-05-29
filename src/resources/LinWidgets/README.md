# LinWidget Developer Guide

LinWidget is the LinDesk desktop widget runtime. Widgets are HTML pages rendered by WebView2 and managed by LinDesk through an isolated WidgetHost process.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Package Structure](#package-structure)
3. [Manifest Reference](#manifest-reference)
4. [Permissions](#permissions)
5. [JavaScript API](#javascript-api)
   - [Identity](#identity)
   - [Host Controls](#host-controls)
   - [System Information](#system-information)
6. [Settings Page](#settings-page)
7. [Layout & Resize](#layout--resize)
8. [Text Selection](#text-selection)
9. [Widget Folders](#widget-folders)
10. [Debug Flow](#debug-flow)
11. [Current Limitations](#current-limitations)

---

## Quick Start

A minimal clock widget in three files.

**`manifest.json`**

```json
{
  "id": "example.hello.widget",
  "name": "Hello Widget",
  "version": "1.0.0",
  "width": 320,
  "height": 140,
  "transparent": true,
  "resizable": true,
  "permissions": ["storage"]
}
```

**`index.html`**

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <main class="widget">
    <div class="title">Hello LinWidget</div>
    <div id="time" class="time">00:00:00</div>
  </main>
  <script src="script.js"></script>
</body>
</html>
```

**`style.css`**

```css
html, body {
  margin: 0;
  width: 100%;
  height: 100%;
  overflow: hidden;
  background: transparent;
  font-family: "Segoe UI", sans-serif;
}

.widget {
  box-sizing: border-box;
  width: 100%;
  height: 100%;
  padding: 16px;
  color: white;
  background: rgba(20, 28, 42, 0.86);
  border: 1px solid rgba(255, 255, 255, 0.22);
  border-radius: 16px;
}
```

**`script.js`**

```js
function updateTime() {
  document.getElementById("time").textContent =
    new Intl.DateTimeFormat(undefined, {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit"
    }).format(new Date());
}

updateTime();
setInterval(updateTime, 1000);
```

---

## Package Structure

```text
MyWidget/
  manifest.json     ← required
  index.html        ← default entry
  settings.html     ← optional settings page
  style.css
  script.js
  assets/
```

`manifest.json` is required. `index.html` is the default entry file. `settings.html` is optional and only loaded when declared in the manifest.

Use responsive CSS so the widget adapts to any host size:

```css
html, body, .widget {
  width: 100%;
  height: 100%;
}
```

---

## Manifest Reference

### Full Example

```json
{
  "id": "example.hello.widget",
  "name": "Hello Widget",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "A short sentence describing what this widget does.",
  "icon": "assets/icon.png",
  "width": 320,
  "height": 140,
  "minWidth": 240,
  "minHeight": 120,
  "maxWidth": 640,
  "maxHeight": 420,
  "type": "html",
  "transparent": true,
  "resizable": true,
  "autoLoad": false,
  "entry": "index.html",
  "settings": "settings.html",
  "permissions": []
}
```

### Fields

| Field | Type | Required | Default | Description |
| --- | --- | --- | --- | --- |
| `id` | string | **yes** | — | Unique widget ID. Use a stable reverse-domain style ID such as `lindesk.clock.lin`. |
| `name` | string | no | `id` | Display name shown in the LinWidgets manager. |
| `version` | string | no | `1.0.0` | Widget version string. |
| `author` | string | no | empty | Author name shown in the manager. |
| `description` | string | no | empty | Short description shown in the manager. |
| `icon` | string | no | default icon | Icon path relative to the widget folder (e.g. `assets/icon.png`). Absolute paths and parent-directory paths are rejected. Falls back to `src/resources/defaultIcon.png` when omitted or missing. |
| `width` | number | **yes** | `300` | Default width in pixels. |
| `height` | number | **yes** | `120` | Default height in pixels. |
| `minWidth` | number | no | `120` | Minimum resize width. |
| `minHeight` | number | no | `80` | Minimum resize height. |
| `maxWidth` | number | no | `0` | Maximum resize width. `0` = unlimited. |
| `maxHeight` | number | no | `0` | Maximum resize height. `0` = unlimited. |
| `type` | string | no | `html` | Runtime type. `html` is supported. `native` shows a placeholder. |
| `transparent` | bool | no | `true` | Enables a transparent host background. Set the HTML background to `transparent` as well when needed. |
| `resizable` | bool | no | `true` | Enables host resize affordances and allows `desktop.resizeWidget()` / `desktop.setWidgetGeometry()`. |
| `autoLoad` | bool | no | `false` | Loads the widget automatically during the LinDesk restore flow. |
| `entry` | string | no | `index.html` | HTML entry file relative to the widget folder. |
| `settings` | string | no | empty | Optional settings page relative to the widget folder. When present, LinDesk shows a Settings action for the widget. |
| `permissions` | array | no | `[]` | Requested permissions. See [Permissions](#permissions). |

---

## Permissions

| Permission | Description |
| --- | --- |
| `network` | Allows `fetch`, `XMLHttpRequest`, `WebSocket`, and `EventSource`. |
| `storage` | Allows `localStorage`, `sessionStorage`, and `indexedDB`. |
| `desktop` | Allows LinDesk desktop APIs such as system information. |

> **Development mode** grants `network`, `storage`, and `desktop` automatically to make debugging easier.

---

## JavaScript API

LinDesk injects `window.desktop` into widget pages and settings pages.

### Identity

```js
console.log(desktop.context);    // "widget" or "settings"
console.log(desktop.widgetId);   // manifest id
console.log(desktop.instanceId); // running LinDesk instance id
console.log(desktop.widgetName); // manifest display name
```

### Host Controls

```js
// Resize the widget host
desktop.resizeWidget({ width: 420, height: 260 });

// Move and resize in one call
desktop.setWidgetGeometry({ x: 100, y: 120, width: 420, height: 260 });

// Show or hide the resize affordance (does not bypass resizable: false)
desktop.setResizeUiVisible(false);
desktop.setResizeUiVisible(true);

// Open the settings dialog
desktop.openSettings();
```

LinDesk clamps the final size with `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, and the current WebView bounds. Resize calls are ignored when `resizable` is `false`.

### System Information

Requires the `desktop` permission:

```json
{ "permissions": ["desktop"] }
```

**One snapshot:**

```js
const info = await desktop.systemInfo.getSnapshot();
console.log(info.cpu.usagePercent);
console.log(info.memory.usagePercent);
```

**Single category** (only collects the requested data):

```js
const cpu     = await desktop.systemInfo.cpu();
const gpu     = await desktop.systemInfo.gpu();
const memory  = await desktop.systemInfo.memory();
const disk    = await desktop.systemInfo.disk();
const network = await desktop.systemInfo.network();
const battery = await desktop.systemInfo.battery();
const os      = await desktop.systemInfo.os();
const uptime  = await desktop.systemInfo.uptime();
```

**Watch refreshed snapshots:**

```js
const watchId = desktop.systemInfo.watch(info => {
  console.log(info.network.downloadBytesPerSecond);
}, 1000); // intervalMs, default 1000, minimum 500

await desktop.systemInfo.unwatch(watchId);
```

**Snapshot shape:**

```js
{
  cpu: {
    name: "CPU name",
    physicalCores: 8,
    logicalCores: 16,
    usagePercent: 12.5
  },
  memory: {
    name: "Memory module name",
    frequencyMHz: 3200,
    totalBytes: 17179869184,
    usedBytes: 8589934592,
    usagePercent: 50,
    modules: [
      {
        name: "Module name",
        locator: "DIMM 0",
        manufacturer: "Memory manufacturer",
        partNumber: "Part number",
        sizeBytes: 8589934592,
        speedMHz: 3200,
        configuredSpeedMHz: 3200,
        frequencyMHz: 3200
      }
    ]
  },
  gpu: {
    name: "GPU name",
    dedicatedVideoMemoryBytes: 8589934592,
    dedicatedSystemMemoryBytes: 0,
    sharedSystemMemoryBytes: 17179869184,
    usagePercent: 18.5,
    adapters: [
      {
        name: "GPU name",
        vendorId: 4318,
        deviceId: 0,
        subSysId: 0,
        revision: 0,
        luid: "adapter-luid",
        dedicatedVideoMemoryBytes: 8589934592,
        dedicatedSystemMemoryBytes: 0,
        sharedSystemMemoryBytes: 17179869184,
        usagePercent: 18.5,
        software: false
      }
    ]
  },
  disk: {
    totalBytes: 1024000000000,
    freeBytes: 512000000000,
    usedBytes: 512000000000,
    drives: [
      {
        root: "C:\\",
        totalBytes: 512000000000,
        freeBytes: 256000000000,
        usedBytes: 256000000000
      }
    ]
  },
  network: {
    downloadBytesPerSecond: 0,
    uploadBytesPerSecond: 0,
    totalReceivedBytes: 123456789,
    totalSentBytes: 9876543
  },
  battery: {
    available: true,
    charging: false,
    acLineOnline: true,
    percent: 88,
    lifeSeconds: 14400
  },
  osVersion: "Windows 11 10.0.26100",
  uptimeSeconds: 3600,
  timestampMs: 1710000000000
}
```

> The first CPU and network sample may return `0` until LinDesk has enough data to calculate a delta. Calls are rejected when the widget lacks the `desktop` permission.

---

## Settings Page

Widgets can provide a full-size settings page for controls that do not fit inside the desktop widget area.

### Declaring a Settings Page

```json
{
  "entry": "index.html",
  "settings": "settings.html",
  "permissions": ["storage"]
}
```

When `settings` is declared and the file exists, LinDesk shows a **Settings** button for the widget. The page opens in a separate resizable LinDesk dialog.

### Shared Origin & Storage

Settings pages share the same widget origin as the main page:

```text
https://<widget-id>.<permissions>.linwidget.local/settings.html
```

`localStorage`, `sessionStorage`, and `indexedDB` are shared between the widget page and settings page when the widget has the `storage` permission.

### Detecting the Settings Context

```js
if (window.desktop && desktop.context === "settings") {
  // Running inside the LinDesk settings dialog.
}
```

### Opening Settings from the Widget

```html
<button id="settingsButton" type="button">Settings</button>
<script>
  document.getElementById("settingsButton").addEventListener("click", () => {
    desktop.openSettings();
  });
</script>
```

When the settings dialog closes, LinDesk reloads the running widget instance so it picks up any updated settings.

### Minimal `settings.html`

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Widget Settings</title>
</head>
<body>
  <label>
    Title
    <input id="titleInput" type="text">
  </label>
  <script>
    const input = document.getElementById("titleInput");
    input.value = localStorage.getItem("title") || "";
    input.addEventListener("input", () => {
      localStorage.setItem("title", input.value);
    });
  </script>
</body>
</html>
```

---

## Layout & Resize

### Resize Affordances

When `resizable` is `true`, LinDesk shows drag handles when the pointer hovers near the widget edge or corner. The widget can also request size changes at runtime:

```js
desktop.resizeWidget({ width: 420, height: 260 });

desktop.setWidgetGeometry({ x: 100, y: 120, width: 420, height: 260 });
```

To show or hide the visible resize handle without disabling resize:

```js
desktop.setResizeUiVisible(false);
desktop.setResizeUiVisible(true);
```

### Saved Layout

Widget sizes and positions are saved to:

```text
%APPDATA%/LinDesk/LinWidgets/linwidget-layout.json
```

Development and installed widgets use separate layout keys:

```text
dev:<manifest-id>
installed:<manifest-id>
```

### Auto Load

Set `autoLoad: true` to have LinDesk load the widget automatically on startup:

```json
{ "autoLoad": true }
```

If a saved layout exists, LinDesk restores that position and size. Otherwise it uses the manifest `width` and `height`.

---

## Text Selection

LinDesk disables text selection inside widgets by default so they feel like desktop controls rather than web pages. Selection remains enabled for:

```text
input
textarea
select
[contenteditable="true"]
.selectable-text
.linwidget-selectable-text
```

Add `selectable-text` or `linwidget-selectable-text` to any element that should remain copyable.

---

## Widget Folders

| Location | Purpose |
| --- | --- |
| `src/resources/LinWidgets/` | Bundled widgets shipped with LinDesk |
| `%APPDATA%/LinDesk/LinWidgets/` | Runtime and user-installed widgets |

Development projects can be added from any local folder through the **Debug LinWidgets** page.

---

## Debug Flow

1. Open the LinDesk **Debug** page.
2. Navigate to the **LinWidgets** tab.
3. Click **Add Dev Project** for a source folder, or **Refresh** for installed widgets.
4. Select a widget and click **Load**.
5. If the widget declares `settings`, click **Settings** to open its full-size settings page.
6. Edit `index.html`, `settings.html`, `style.css`, `script.js`, or `manifest.json` — changes trigger hot reload automatically.

---

## Current Limitations

| # | Limitation |
| --- | --- |
| 1 | HTML entries are loaded through the LinWidget virtual host origin. |
| 2 | Settings pages are widget-provided HTML. LinDesk does not generate forms from a JSON schema yet. |
| 3 | The Settings button is currently only available in the Debug LinWidgets list. Widget right-click menu integration is planned. |
