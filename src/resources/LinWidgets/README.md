# LinWidget Developer Guide

LinWidget is the LinDesk desktop widget runtime. It supports HTML widgets rendered by WebView2 and managed by LinDesk through an isolated WidgetHost process.

## Widget Folders

Bundled widgets live in:

```text
src/resources/LinWidgets
```

Runtime and user installed widgets live in:

```text
%APPDATA%/LinDesk/LinWidgets
```

Development projects can be added from any local folder through the Debug LinWidgets page.

## Package Structure

```text
MyWidget/
  manifest.json
  index.html
  settings.html
  style.css
  script.js
  assets/
```

`manifest.json` is required. `index.html` is the default entry file. `settings.html` is optional and is only loaded when the manifest declares it.

## Manifest Example

```json
{
  "id": "example.hello.widget",
  "name": "Hello Widget",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "A short sentence describing what this widget does.",
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

## Manifest Fields

| Field | Type | Required | Default | Description |
| --- | --- | --- | --- | --- |
| `id` | string | yes | none | Unique widget id. Use a stable id such as `lindesk.clock.lin`. |
| `name` | string | no | `id` | Display name in the LinWidgets manager. |
| `version` | string | no | `1.0.0` | Widget version. |
| `author` | string | no | empty | Widget author name shown in the LinWidgets manager. |
| `description` | string | no | empty | Short widget description shown in the LinWidgets manager. |
| `width` | number | yes | `300` | Default width in pixels. |
| `height` | number | yes | `120` | Default height in pixels. |
| `minWidth` | number | no | `120` | Minimum resize width. |
| `minHeight` | number | no | `80` | Minimum resize height. |
| `maxWidth` | number | no | `0` | Maximum resize width. `0` means unlimited. |
| `maxHeight` | number | no | `0` | Maximum resize height. `0` means unlimited. |
| `type` | string | no | `html` | Runtime type. `html` is supported. `native` shows a placeholder for now. |
| `transparent` | bool | no | `true` | Enables transparent host background. HTML should also use a transparent background when needed. |
| `resizable` | bool | no | `true` | Enables LinDesk host resize UI and allows size changes through `desktop.resizeWidget()` and `desktop.setWidgetGeometry()`. |
| `autoLoad` | bool | no | `false` | Automatically loads the widget when LinDesk restores LinWidgets from the default registry and layout config. |
| `entry` | string | no | `index.html` | HTML entry file relative to the widget folder. |
| `settings` | string | no | empty | Optional HTML settings page relative to the widget folder. When present, LinDesk shows a Settings action for the widget. |
| `permissions` | array | no | `[]` | Requested permissions. Supported values are `network`, `storage`, and `desktop`. |

## Resize Support

If `resizable` is true, LinDesk shows host resize affordances when the pointer hovers near the widget edge or corner. The widget can also request size changes through the JavaScript API.

```json
{
  "resizable": true
}
```

The widget can request size changes:

```js
desktop.resizeWidget({
  width: 420,
  height: 260
});

desktop.setWidgetGeometry({
  x: 100,
  y: 120,
  width: 420,
  height: 260
});
```

LinDesk still clamps the final size with `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, and the current WebView bounds. If `resizable` is false, resize API requests are ignored.

Widget authors can hide or show the host resize UI at runtime:

```js
desktop.setResizeUiVisible(false);
desktop.setResizeUiVisible(true);
```

This only controls the visible host resize affordance. It does not bypass `resizable`.

Example resize button:

```html
<button id="growButton" type="button">Grow</button>
<script>
document.getElementById("growButton").addEventListener("click", () => {
  desktop.resizeWidget({ width: 420, height: 260 });
});
</script>
```

Size is saved to:

```text
%APPDATA%/LinDesk/LinWidgets/linwidget-layout.json
```

Development and installed widgets have separate layout keys:

```text
dev:<manifest-id>
installed:<manifest-id>
```

## Auto Load

Set `autoLoad` to true to let LinDesk load the widget automatically during the normal LinWidget restore flow:

```json
{
  "autoLoad": true
}
```

Auto loaded widgets still use the same layout rules. If saved layout exists, LinDesk restores that position and size. If no saved layout exists, LinDesk starts the widget from its manifest `width` and `height`.

Use responsive CSS so the widget adapts to the host size:

```css
html,
body,
.widget {
  width: 100%;
  height: 100%;
}
```

## Permissions

| Permission | Description |
| --- | --- |
| `network` | Allows `fetch`, `XMLHttpRequest`, `WebSocket`, and `EventSource`. |
| `storage` | Allows `localStorage`, `sessionStorage`, and `indexedDB`. |
| `desktop` | Allows LinDesk desktop APIs such as system information. |

Development mode grants `network`, `storage`, and `desktop` by default to make debugging easier.

## Supported Desktop APIs

LinDesk injects `window.desktop` into widget pages and settings pages.

Identity fields:

```js
console.log(desktop.context);    // "widget" or "settings"
console.log(desktop.widgetId);   // manifest id
console.log(desktop.instanceId); // running LinDesk instance id
console.log(desktop.widgetName); // manifest display name
```

Widget host controls:

```js
desktop.resizeWidget({
  width: 420,
  height: 260
});

desktop.setWidgetGeometry({
  x: 100,
  y: 120,
  width: 420,
  height: 260
});

desktop.setResizeUiVisible(false);
desktop.setResizeUiVisible(true);
desktop.openSettings();
```

System information APIs require the `desktop` permission:

```json
{
  "permissions": ["desktop"]
}
```

Read one snapshot:

```js
const info = await desktop.systemInfo.getSnapshot();
console.log(info.cpu.usagePercent);
console.log(info.memory.usagePercent);
```

Read one category:

```js
const cpu = await desktop.systemInfo.cpu();
const gpu = await desktop.systemInfo.gpu();
const memory = await desktop.systemInfo.memory();
const disk = await desktop.systemInfo.disk();
const network = await desktop.systemInfo.network();
const battery = await desktop.systemInfo.battery();
const os = await desktop.systemInfo.os();
const uptime = await desktop.systemInfo.uptime();
```

Category calls only collect the requested data. For example, `desktop.systemInfo.cpu()` does not collect GPU, disk, network, or battery data.

Watch refreshed snapshots:

```js
const watchId = desktop.systemInfo.watch(info => {
  console.log(info.network.downloadBytesPerSecond);
}, 1000);

await desktop.systemInfo.unwatch(watchId);
```

`watch(callback, intervalMs)` defaults to `1000` ms. Values below `500` ms are clamped to `500` ms.

Snapshot shape:

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

The first CPU or network sample can return `0` until LinDesk has enough data to calculate a delta. Calls are rejected when the widget does not have the `desktop` permission.

## Settings Page

Widgets can provide a full size settings page for controls that do not fit inside the small desktop widget area.

```json
{
  "entry": "index.html",
  "settings": "settings.html",
  "permissions": ["storage"]
}
```

When `settings` is declared and the file exists, LinDesk shows a Settings button for the widget in the Debug LinWidgets list. The settings page opens in a separate resizable LinDesk dialog.

Settings pages are loaded from the same widget package and use the same widget origin as the main page:

```text
https://<widget-id>.<permissions>.linwidget.local/settings.html
```

This means browser storage such as `localStorage`, `sessionStorage`, and `indexedDB` is shared between the widget page and its settings page when the widget has the `storage` permission. Without `storage`, those storage APIs are blocked in both pages.

LinDesk injects a small context marker:

```js
if (window.desktop && desktop.context === "settings") {
  // Running inside the LinDesk settings dialog.
}
```

The main widget page can open its own settings page through the host API:

```html
<button id="settingsButton" type="button">Settings</button>
<script>
  document.getElementById("settingsButton").addEventListener("click", () => {
    desktop.openSettings();
  });
</script>
```

Widget pages and settings pages can also read identity fields:

```js
console.log(desktop.context);    // "widget" or "settings"
console.log(desktop.widgetId);   // manifest id
console.log(desktop.instanceId); // running LinDesk instance id
console.log(desktop.widgetName); // manifest display name
```

Minimal `settings.html` example:

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

When the settings dialog closes, LinDesk reloads the running widget instance so it can read the updated settings.

## Text Selection

LinDesk disables normal text selection inside widgets by default so widgets feel like desktop controls instead of regular web pages.

Inputs and editable areas still allow text selection:

```text
input
textarea
select
[contenteditable="true"]
.selectable-text
.linwidget-selectable-text
```

Use `selectable-text` or `linwidget-selectable-text` when a non input text area should remain copyable.

## Minimal HTML Example

`index.html`:

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

`style.css`:

```css
html,
body {
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

`script.js`:

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

## Debug Flow

1. Open the LinDesk Debug page.
2. Open the LinWidgets page.
3. Use Add Dev Project for a source folder, or Refresh for installed widgets.
4. Select a widget.
5. Click Load in the widget row.
6. If the widget declares `settings`, click Settings to open its full size settings page.
7. For development projects, edit `index.html`, `settings.html`, `style.css`, `script.js`, or `manifest.json` to trigger hot reload.

## Current Limitations

1. HTML entries are loaded through the LinWidget virtual host.
2. Version 1 settings pages are widget provided HTML pages. LinDesk does not generate forms from schema yet.
3. Settings integration starts from the Debug LinWidgets list. Widget right click menu integration can reuse the same dialog later.
