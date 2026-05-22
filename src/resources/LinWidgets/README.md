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
  style.css
  script.js
  assets/
```

`manifest.json` is required. `index.html` is the default entry file.

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
| `resizable` | bool | no | `true` | Allows size changes through `desktop.resizeWidget()` and `desktop.setWidgetGeometry()`. LinDesk does not draw a resize grip. |
| `autoLoad` | bool | no | `false` | Automatically loads the widget when LinDesk restores LinWidgets from the default registry and layout config. |
| `entry` | string | no | `index.html` | HTML entry file relative to the widget folder. |
| `permissions` | array | no | `[]` | Requested permissions. Supported values are `network`, `storage`, and `desktop`. |

## Resize Support

If `resizable` is true, the widget can request size changes through the JavaScript API. LinDesk does not draw a resize grip, so widget authors should provide their own resize UI when user controlled resizing is needed.

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
| `desktop` | Reserved for future `desktop.*` APIs from LinDesk. |

Development mode grants `network`, `storage`, and `desktop` by default to make debugging easier.

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
5. Click Start.
6. For development projects, edit `index.html`, `style.css`, `script.js`, or `manifest.json` to trigger hot reload.

## Current Limitations

1. HTML entries are still loaded from local files.
2. The formal `desktop.*` JavaScript bridge is reserved for future versions.
3. The first resize version supports the bottom right grip only.
