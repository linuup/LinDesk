# LinWidget Developer Guide

LinWidget is the LinDesk desktop widget runtime. Widgets are HTML pages rendered by CEF OSR (Chromium Embedded Framework Off-Screen Rendering) and managed by LinDesk.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Package Structure](#package-structure)
3. [Manifest Reference](#manifest-reference)
4. [Permissions](#permissions)
5. [JavaScript API](#javascript-api)
   - [Identity](#identity)
   - [Host Controls](#host-controls)
   - [Multi Instance](#multi-instance)
   - [Storage](#storage)
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
  "multiInstance": false,
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
| `multiInstance` | bool | no | `false` | Allows the widget to create or load multiple independent instances. LinDesk currently allows up to 3 instances per widget. |
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

console.log(desktop.instance.id);            // same as desktop.instanceId
console.log(desktop.instance.registryId);    // manifest id used by LinDesk storage
console.log(desktop.instance.multiInstance); // true when manifest multiInstance is enabled
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

LinDesk clamps the final size with `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, and the current widget bounds. Resize calls are ignored when `resizable` is `false`.

### Multi Instance

Multi instance is opt in. Add `multiInstance: true` to `manifest.json`:

```json
{
  "id": "example.notes.widget",
  "name": "Notes",
  "multiInstance": true,
  "permissions": ["storage", "desktop"]
}
```

Single instance widgets keep the stable instance id:

```text
installed:example.notes.widget
dev:example.notes.widget
```

Multi instance widgets use an extra suffix:

```text
installed:example.notes.widget:<shortId>
dev:example.notes.widget:<shortId>
```

LinDesk currently allows at most 3 running or saved instances for the same widget.

#### `desktop.instances.create(options)`

Creates a new instance for the same widget family.

```js
const result = await desktop.instances.create({
  x: 320,
  y: 240,
  width: 360,
  height: 260
});

if (result.ok) {
  console.log(result.instanceId);
} else {
  console.warn(result.error);
}
```

This API requires the `desktop` permission. If `options` is omitted, LinDesk creates the new instance near the source instance and clamps it to the visible desktop area:

```js
const result = await desktop.instances.create();
```

Success response:

```js
{
  ok: true,
  instanceId: "installed:example.notes.widget:01hxyz"
}
```

#### `desktop.instances.list()`

Returns all saved instances for the same manifest `id` and runtime mode. A development widget only sees development instances. An installed widget only sees installed instances.

```js
const result = await desktop.instances.list();
if (result.ok) {
  for (const item of result.instances) {
    console.log(item.instanceId, item.running, item.geometry);
  }
}
```

Response shape:

```js
{
  ok: true,
  instances: [
    {
      instanceId: "installed:example.notes.widget:01hxyz",
      id: "installed:example.notes.widget:01hxyz",
      visible: true,
      running: true,
      parentInstanceId: "installed:example.notes.widget",
      createdAt: "2026-06-02T10:00:00Z",
      geometry: {
        x: 320,
        y: 240,
        width: 360,
        height: 260
      }
    }
  ]
}
```

#### `desktop.instances.close(instanceId)`

Hides one instance from the desktop. It stops the iframe and marks the layout record as hidden. Instance data and the layout record remain available for later restore.

If `instanceId` is omitted, the current instance is closed:

```js
await desktop.instances.close();
```

To close another instance from the same widget family:

```js
await desktop.instances.close("installed:example.notes.widget:01hxyz");
```

The current instance shortcut calls the same API:

```js
await desktop.instance.close();
```

#### `desktop.instances.remove(instanceId)`

Deletes one saved instance layout record. It also stops the iframe when that instance is running. It does not remove the installed widget package, registry entry, shared widget storage, or other instances.

If `instanceId` is omitted, the current instance is removed:

```js
await desktop.instances.remove();
```

To remove another instance from the same widget family:

```js
await desktop.instances.remove("installed:example.notes.widget:01hxyz");
```

The current instance shortcut calls the same API:

```js
await desktop.instance.remove();
```

All instance APIs return objects instead of throwing:

```js
{
  ok: false,
  error: "Target widget instance is outside the caller scope."
}
```

`create`, `close`, and `remove` should be called after saving user data. `list`, `close`, and `remove` are scoped to the caller widget family, so a widget cannot manage another widget's instances.

### Storage

LinWidget has two storage paths:

1. Browser storage: `localStorage`, `sessionStorage`, and `indexedDB`
2. LinDesk storage API: `desktop.storage.instance` and `desktop.storage.widget`

Browser storage still follows the browser engine origin rules. For multi instance widgets, every instance can have an isolated browser origin. Use the LinDesk storage API when several instances need shared data.

Both storage APIs require the `storage` permission:

```json
{ "permissions": ["storage"] }
```

#### Instance Storage

`desktop.storage.instance` is private to the current running instance:

```js
await desktop.storage.instance.set("draft", {
  title: "Private note",
  body: "Only this instance can read it."
});

const result = await desktop.storage.instance.get("draft");
if (result.ok) {
  console.log(result.value);
}

await desktop.storage.instance.remove("draft");
await desktop.storage.instance.clear();
```

#### Widget Storage

`desktop.storage.widget` is shared by all instances with the same manifest `id`. This is the recommended storage for note lists, shared preferences, or data that a settings page should manage for all instances:

```js
const notes = [
  { id: "note-1", text: "Shared across all note instances" }
];

await desktop.storage.widget.set("notes", notes);

const result = await desktop.storage.widget.get("notes");
if (result.ok) {
  renderNotes(result.value || []);
}
```

You can read the full object with `all()`:

```js
const result = await desktop.storage.widget.all();
if (result.ok) {
  console.log(result.value);
}
```

#### Return Shape

Every storage call returns:

```js
{
  ok: true,
  value: {}
}
```

or:

```js
{
  ok: false,
  error: "Storage permission is required."
}
```

LinDesk derives the storage namespace from the real running instance. Widgets cannot pass another widget id to read another widget's data.

Data is stored under:

```text
%APPDATA%/LinDesk/LinWidgetsData/
```

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

---

## Example: Notes Widget Implementation

The Notes widget (`Notes/`) is a multi-instance widget that demonstrates settings page integration, real-time synchronization, and instance lifecycle management.

### Multi Instance

The Notes widget enables `multiInstance: true` in its manifest and provides two header buttons for instance management:

- **+** (Create): Calls `desktop.instances.create()` to spawn a new independent note instance.
- **×** (Close): Calls `desktop.instances.close()` to close the current instance.

```js
// Create a new note instance
createInstanceBtn.addEventListener('click', async () => {
  if (window.desktop && desktop.instances) {
    const result = await desktop.instances.create();
    if (!result.ok) {
      console.warn('Failed to create note instance:', result.error);
    }
  }
});

// Close the current note instance
closeInstanceBtn.addEventListener('click', () => {
  if (window.desktop && desktop.instances) {
    desktop.instances.close();
  }
});
```

### Stored Configurations

All settings are saved directly in `localStorage` under the shared origin:

1. `lin_sticky_notes_theme`: Current visual theme. Supported values:
   - `yellow` (Lemon Warm, default)
   - `green` (Mint Fresh)
   - `blue` (Sky Calm)
   - `pink` (Cherry Sweet)
   - `white` (Classic Light)
   - `dark` (Dark Tech)
2. `lin_sticky_notes_fontsize`: Default editor font size (e.g. `16px`).
3. `lin_sticky_notes_zoom_key`: Modifier key combination for mouse wheel font zooming:
   - `all` (Ctrl, Alt, or Shift)
   - `ctrl`
   - `alt`
   - `shift`
   - `none` (Disable wheel zoom)
4. `lin_sticky_notes_confirm_clear`: Whether to prompt a confirmation card before clearing the note (`true` or `false`).
5. `lin_sticky_notes_opacity`: Opacity level of the widget (range: `30` - `100`, default: `100`).

### Real-Time Updates

To achieve instant response without waiting for the settings window to close, the main widget (`index.html`) listens to the `storage` event:

```javascript
window.addEventListener('storage', (event) => {
  if (event.key === 'lin_sticky_notes_theme') {
    document.body.className = `theme-${event.newValue || 'yellow'}`;
  } else if (event.key === 'lin_sticky_notes_fontsize') {
    if (event.newValue) {
      noteArea.style.fontSize = event.newValue;
    }
  } else if (event.key === 'lin_sticky_notes_opacity') {
    if (event.newValue) {
      applyOpacity(event.newValue);
    }
  }
});
```
