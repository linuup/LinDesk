(() => {
  const surface = document.getElementById('surface');
  const widgets = new Map();
  let active = null;
  let pendingGeometryFrame = 0;
  const pendingGeometryIds = new Set();
  const loadTimeoutMs = 8000;
  const maxAutoReloadAttempts = 2;
  const resizeHitSize = 8;
  const resizeCursorByMode = {
    n: 'ns-resize',
    s: 'ns-resize',
    e: 'ew-resize',
    w: 'ew-resize',
    ne: 'nesw-resize',
    sw: 'nesw-resize',
    nw: 'nwse-resize',
    se: 'nwse-resize'
  };

  const post = message => {
    try {
      chrome.webview.postMessage(message);
    } catch (_) {
    }
  };

  const describeTarget = target => {
    if (!target) {
      return '';
    }
    const tag = String(target.tagName || '').toLowerCase();
    const id = target.id ? `#${target.id}` : '';
    const className = target.className ? `.${String(target.className).replace(/\s+/g, '.')}` : '';
    return `${tag}${id}${className}`;
  };

  const clampNumber = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const clampRange = (value, min, max) => Math.min(Math.max(value, min), max);
  const surfaceBounds = () => ({
    width: Math.max(1, surface ? surface.clientWidth : window.innerWidth),
    height: Math.max(1, surface ? surface.clientHeight : window.innerHeight)
  });

  const widgetContextMessage = (id, record) => ({
    type: 'linwidget_context',
    context: 'widget',
    instanceId: id,
    widgetId: record && record.manifestId ? record.manifestId : '',
    widgetName: record && record.name ? record.name : ''
  });

  const sendWidgetContext = id => {
    const record = widgets.get(id);
    if (!record || !record.iframe || !record.iframe.contentWindow) {
      return;
    }
    try {
      record.iframe.contentWindow.postMessage(widgetContextMessage(id, record), '*');
    } catch (_) {
    }
  };

  const sendWidgetMessage = (id, message) => {
    const record = widgets.get(id);
    if (!record || !record.iframe || !record.iframe.contentWindow) {
      return;
    }
    try {
      record.iframe.contentWindow.postMessage(message, '*');
    } catch (_) {
    }
  };

  const injectWidgetSelectionStyle = iframe => {
    try {
      const doc = iframe.contentDocument;
      if (!doc || doc.getElementById('linwidget-selection-style')) {
        return true;
      }
      const style = doc.createElement('style');
      style.id = 'linwidget-selection-style';
      style.textContent = `
html,
body,
body * {
  user-select: none !important;
  -webkit-user-select: none !important;
}

input,
textarea,
select,
[contenteditable="true"],
[contenteditable="true"] *,
.selectable-text,
.selectable-text *,
.linwidget-selectable-text,
.linwidget-selectable-text * {
  user-select: text !important;
  -webkit-user-select: text !important;
}`;
      const target = doc.head || doc.documentElement;
      if (target) {
        target.appendChild(style);
      }
      return true;
    } catch (_) {
      return false;
    }
  };

  const clampSize = (geometry, constraints = {}) => {
    const minWidth = Math.max(1, clampNumber(constraints.minWidth, 1));
    const minHeight = Math.max(1, clampNumber(constraints.minHeight, 1));
    const rawMaxWidth = clampNumber(constraints.maxWidth, 0);
    const rawMaxHeight = clampNumber(constraints.maxHeight, 0);
    const maxWidth = rawMaxWidth > 0 ? Math.max(minWidth, rawMaxWidth) : Infinity;
    const maxHeight = rawMaxHeight > 0 ? Math.max(minHeight, rawMaxHeight) : Infinity;
    return Object.assign({}, geometry, {
      width: Math.min(maxWidth, Math.max(minWidth, clampNumber(geometry && geometry.width, 300))),
      height: Math.min(maxHeight, Math.max(minHeight, clampNumber(geometry && geometry.height, 120)))
    });
  };

  const clampGeometry = (geometry, constraints = {}) => {
    const bounds = surfaceBounds();
    const size = clampSize(geometry, constraints);
    size.width = Math.min(size.width, bounds.width);
    size.height = Math.min(size.height, bounds.height);
    const maxX = Math.max(0, bounds.width - size.width);
    const maxY = Math.max(0, bounds.height - size.height);
    return Object.assign({}, size, {
      x: clampRange(clampNumber(geometry && geometry.x, 0), 0, maxX),
      y: clampRange(clampNumber(geometry && geometry.y, 0), 0, maxY)
    });
  };

  const applyGeometry = (panel, geometry, constraints) => {
    const next = clampGeometry(geometry, constraints);
    panel.style.left = `${next.x}px`;
    panel.style.top = `${next.y}px`;
    panel.style.width = `${next.width}px`;
    panel.style.height = `${next.height}px`;
    return next;
  };

  const reclampAllWidgets = (notify = false) => {
    for (const [id, record] of widgets) {
      const before = geometryOf(record.panel);
      applyGeometry(record.panel, before, record.constraints);
      if (notify) {
        const after = geometryOf(record.panel);
        if (before.x !== after.x || before.y !== after.y || before.width !== after.width || before.height !== after.height) {
          notifyGeometry(id, false);
        }
      }
    }
  };

  const geometryOf = panel => ({
    x: Math.round(panel.offsetLeft),
    y: Math.round(panel.offsetTop),
    width: Math.round(panel.offsetWidth),
    height: Math.round(panel.offsetHeight)
  });

  const resizeModeAt = (panel, event) => {
    const rect = panel.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const nearLeft = x >= 0 && x <= resizeHitSize;
    const nearRight = x >= rect.width - resizeHitSize && x <= rect.width;
    const nearTop = y >= 0 && y <= resizeHitSize;
    const nearBottom = y >= rect.height - resizeHitSize && y <= rect.height;
    const vertical = nearTop ? 'n' : nearBottom ? 's' : '';
    const horizontal = nearLeft ? 'w' : nearRight ? 'e' : '';
    return vertical + horizontal;
  };

  const updateResizeHover = (record, event) => {
    if (!record || record.resizable === false || record.resizeUiVisible === false || active) {
      if (record) {
        record.panel.dataset.resizeMode = '';
        record.panel.style.cursor = '';
      }
      return '';
    }
    const mode = resizeModeAt(record.panel, event);
    record.panel.dataset.resizeMode = mode;
    record.panel.style.cursor = resizeCursorByMode[mode] || '';
    return mode;
  };

  const startAction = (event, id, mode = 'move') => {
    if (event.button !== 0) {
      return;
    }
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    active = {
      id,
      mode,
      startX: event.screenX,
      startY: event.screenY,
      startGeometry: geometryOf(record.panel)
    };
    document.body.classList.toggle('dragging', mode === 'move');
    document.body.classList.toggle('resizing', mode !== 'move');
    record.panel.classList.toggle('active-resize', mode !== 'move');
    try {
      event.currentTarget.setPointerCapture(event.pointerId);
    } catch (_) {
    }
    event.preventDefault();
  };

  const notifyGeometry = (id, live = false) => {
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    post(Object.assign({
      type: 'widget_geometry_changed',
      widgetId: id,
      instanceId: id,
      live
    }, geometryOf(record.panel)));
  };

  const iframeErrorText = iframe => {
    try {
      const doc = iframe.contentDocument;
      if (!doc) {
        return '';
      }
      return `${doc.title || ''} ${doc.body ? doc.body.innerText || '' : ''}`.toLowerCase();
    } catch (_) {
      return '';
    }
  };

  const reloadIframe = (id, reason = 'manual') => {
    const record = widgets.get(id);
    if (!record || !record.iframe) {
      return false;
    }
    try {
      const url = new URL(record.iframe.src || record.entryUrl || 'about:blank');
      url.searchParams.set('__linwidget_reload', String(Date.now()));
      record.iframe.src = url.toString();
    } catch (_) {
      record.iframe.src = record.iframe.src;
    }
    record.loaded = false;
    if (record.loadTimer) {
      clearTimeout(record.loadTimer);
    }
    record.loadTimer = setTimeout(() => recoverWidgetLoad(id, 'timeout'), loadTimeoutMs);
    post({ type: 'console_log', level: 'warning', message: `auto reload ${id}: ${reason}, attempt ${record.reloadAttempts || 0}` });
    return true;
  };

  const recoverWidgetLoad = (id, reason) => {
    const record = widgets.get(id);
    if (!record || record.loaded) {
      return;
    }
    record.reloadAttempts = (record.reloadAttempts || 0) + 1;
    if (record.reloadAttempts > maxAutoReloadAttempts) {
      post({ type: 'widget_error', widgetId: id, instanceId: id, message: `iframe load recovery failed after ${maxAutoReloadAttempts} attempt(s): ${reason}` });
      return;
    }
    reloadIframe(id, reason);
  };

  const scheduleLiveGeometry = id => {
    pendingGeometryIds.add(id);
    if (pendingGeometryFrame) {
      return;
    }
    pendingGeometryFrame = requestAnimationFrame(() => {
      pendingGeometryFrame = 0;
      const ids = Array.from(pendingGeometryIds);
      pendingGeometryIds.clear();
      ids.forEach(itemId => notifyGeometry(itemId, true));
    });
  };

  const addWidget = message => {
    const id = String(message.instanceId || message.widgetId || '').trim();
    if (!id) {
      return;
    }
    removeWidget({ instanceId: id });

    const panel = document.createElement('section');
    panel.className = 'linwidget-panel';
    panel.dataset.instanceId = id;
    panel.style.zIndex = String(clampNumber(message.zOrder, widgets.size + 1));
    const constraints = {
      minWidth: message.minWidth,
      minHeight: message.minHeight,
      maxWidth: message.maxWidth,
      maxHeight: message.maxHeight
    };

    const iframe = document.createElement('iframe');
    iframe.src = message.entryUrl || 'about:blank';
    iframe.allow = 'clipboard-read; clipboard-write';
    iframe.addEventListener('load', () => {
      const record = widgets.get(id);
      const errorText = iframeErrorText(iframe);
      if (errorText.includes('took too long to respond') || errorText.includes('this site can')) {
        if (record) {
          record.loaded = false;
        }
        recoverWidgetLoad(id, 'webview error page');
        return;
      }
      if (record) {
        record.loaded = true;
        record.reloadAttempts = 0;
        if (record.loadTimer) {
          clearTimeout(record.loadTimer);
          record.loadTimer = 0;
        }
      }
      if (!injectWidgetSelectionStyle(iframe)) {
        post({ type: 'console_log', level: 'warning', message: `selection style injection skipped ${id}: ${iframe.src}` });
      }
      sendWidgetContext(id);
      post({ type: 'console_log', level: 'info', message: `iframe loaded ${id}: ${iframe.src}` });
    });
    iframe.addEventListener('error', () => {
      recoverWidgetLoad(id, 'iframe error');
      post({ type: 'widget_error', widgetId: id, instanceId: id, message: `iframe failed to load: ${iframe.src}` });
    });

    const dragHandle = document.createElement('div');
    dragHandle.className = 'linwidget-drag-handle';

    const shield = document.createElement('div');
    shield.className = 'linwidget-drag-shield';

    const resizeFrame = document.createElement('div');
    resizeFrame.className = 'linwidget-resize-frame';
    const resizeModes = ['n', 's', 'e', 'w', 'ne', 'nw', 'se', 'sw'];

    const resizable = message.resizable !== false;

    dragHandle.addEventListener('pointerdown', event => startAction(event, id, 'move'));
    panel.addEventListener('pointermove', event => updateResizeHover(widgets.get(id), event));
    panel.addEventListener('pointerleave', () => {
      const record = widgets.get(id);
      if (!record || active) {
        return;
      }
      record.panel.dataset.resizeMode = '';
      record.panel.style.cursor = '';
    });
    panel.addEventListener('pointerdown', event => {
      const record = widgets.get(id);
      const mode = updateResizeHover(record, event);
      if (mode && record && record.resizable !== false && record.resizeUiVisible !== false) {
        startAction(event, id, mode);
      }
    }, true);
    const resizeHandles = resizeModes.map(mode => {
      const handle = document.createElement('div');
      handle.className = `linwidget-resize-hit linwidget-resize-${mode}`;
      handle.dataset.mode = mode;
      handle.style.cursor = resizeCursorByMode[mode] || '';
      handle.addEventListener('pointerenter', () => {
        const record = widgets.get(id);
        if (!record || record.resizable === false || record.resizeUiVisible === false || active) {
          return;
        }
        record.panel.dataset.resizeMode = mode;
        record.panel.style.cursor = resizeCursorByMode[mode] || '';
      });
      handle.addEventListener('pointerleave', () => {
        const record = widgets.get(id);
        if (!record || active) {
          return;
        }
        record.panel.dataset.resizeMode = '';
        record.panel.style.cursor = '';
      });
      handle.addEventListener('pointerdown', event => {
        const record = widgets.get(id);
        if (!record || record.resizable === false || record.resizeUiVisible === false) {
          return;
        }
        record.panel.dataset.resizeMode = mode;
        record.panel.style.cursor = resizeCursorByMode[mode] || '';
        startAction(event, id, mode);
      });
      return handle;
    });

    panel.appendChild(iframe);
    panel.appendChild(dragHandle);
    panel.appendChild(shield);
    panel.appendChild(resizeFrame);
    resizeHandles.forEach(handle => panel.appendChild(handle));
    widgets.set(id, {
      panel,
      iframe,
      entryUrl: String(message.entryUrl || '').trim(),
      loaded: false,
      loadTimer: 0,
      reloadAttempts: 0,
      constraints,
      resizable,
      resizeUiVisible: true,
      development: Boolean(message.development),
      manifestId: String(message.manifestId || '').trim(),
      name: String(message.name || '').trim(),
      permissions: Array.isArray(message.permissions) ? message.permissions : []
    });
    applyGeometry(panel, message.geometry || message, constraints);
    surface.appendChild(panel);
    const record = widgets.get(id);
    if (record) {
      record.loadTimer = setTimeout(() => recoverWidgetLoad(id, 'timeout'), loadTimeoutMs);
    }
    post({ type: 'console_log', level: 'info', message: `added widget ${id}: ${iframe.src}` });
  };

  const removeWidget = message => {
    const id = String(message.instanceId || message.widgetId || '').trim();
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    if (record.loadTimer) {
      clearTimeout(record.loadTimer);
    }
    record.panel.remove();
    widgets.delete(id);
  };

  const updateWidgetGeometry = message => {
    const id = String(message.instanceId || message.widgetId || '').trim();
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    applyGeometry(record.panel, message.geometry || message, record.constraints);
  };

  const reloadWidget = message => {
    const id = String(message.instanceId || message.widgetId || '').trim();
    reloadIframe(id, 'reload message');
  };

  const updateHostOptions = message => {
    document.body.classList.toggle('show-webview-bounds', Boolean(message.showWebViewBounds));
  };

  const widgetIdForWindow = source => {
    for (const [id, record] of widgets) {
      if (record.iframe && record.iframe.contentWindow === source) {
        return id;
      }
    }
    return '';
  };

  const applyWidgetGeometryRequest = (id, message, includePosition) => {
    const record = widgets.get(id);
    if (!record || record.resizable === false) {
      return;
    }
    const current = geometryOf(record.panel);
    const requested = Object.assign({}, current, message.geometry || message);
    if (!includePosition) {
      requested.x = current.x;
      requested.y = current.y;
    }
    applyGeometry(record.panel, requested, record.constraints);
    notifyGeometry(id, false);
  };

  const hasPermission = (record, permission) => {
    if (!record || record.development === true) {
      return true;
    }
    return Array.isArray(record.permissions) && record.permissions.includes(permission);
  };

  const denyPermission = (id, permission, url = '') => {
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    console.warn(url ? `permission denied: ${permission} ${url}` : `permission denied: ${permission}`);
    post({
      type: 'permission_denied',
      widgetId: id,
      instanceId: id,
      permission,
      url
    });
  };

  const applyResizeUiRequest = (id, message) => {
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    record.resizeUiVisible = Boolean(message.visible);
    record.panel.classList.toggle('resize-ui-hidden', !record.resizeUiVisible);
    record.panel.dataset.resizeMode = '';
    record.panel.style.cursor = '';
  };

  window.addEventListener('pointermove', event => {
    if (!active) {
      return;
    }
    const record = widgets.get(active.id);
    if (!record) {
      return;
    }
    const dx = event.screenX - active.startX;
    const dy = event.screenY - active.startY;
    const next = Object.assign({}, active.startGeometry);
    if (active.mode === 'move') {
      next.x += dx;
      next.y += dy;
    } else {
      if (active.mode.includes('e')) {
        next.width += dx;
      }
      if (active.mode.includes('s')) {
        next.height += dy;
      }
      if (active.mode.includes('w')) {
        next.x += dx;
        next.width -= dx;
      }
      if (active.mode.includes('n')) {
        next.y += dy;
        next.height -= dy;
      }
    }
    const applied = applyGeometry(record.panel, next, record.constraints);
    if (active.mode !== 'move') {
      if (active.mode.includes('w')) {
        applied.x = active.startGeometry.x + active.startGeometry.width - applied.width;
      }
      if (active.mode.includes('n')) {
        applied.y = active.startGeometry.y + active.startGeometry.height - applied.height;
      }
      applyGeometry(record.panel, applied, record.constraints);
    }
    scheduleLiveGeometry(active.id);
    event.preventDefault();
  });

  window.addEventListener('pointerup', () => {
    if (!active) {
      return;
    }
    const id = active.id;
    const record = widgets.get(id);
    active = null;
    document.body.classList.remove('dragging');
    document.body.classList.remove('resizing');
    if (record) {
      record.panel.classList.remove('active-resize');
      record.panel.dataset.resizeMode = '';
      record.panel.style.cursor = '';
    }
    notifyGeometry(id, false);
  });

  window.addEventListener('pointercancel', () => {
    if (active) {
      const record = widgets.get(active.id);
      if (record) {
        record.panel.classList.remove('active-resize');
        record.panel.dataset.resizeMode = '';
        record.panel.style.cursor = '';
      }
    }
    active = null;
    document.body.classList.remove('dragging');
    document.body.classList.remove('resizing');
  });

  window.addEventListener('resize', () => reclampAllWidgets(true));

  window.addEventListener('keydown', event => {
    post({
      type: 'host_keydown',
      key: event.key || '',
      code: event.code || '',
      target: describeTarget(event.target),
      defaultPrevented: Boolean(event.defaultPrevented)
    });
  }, true);

  window.addEventListener('message', event => {
    const message = event.data || {};
    if (!message || typeof message !== 'object') {
      return;
    }
    const id = widgetIdForWindow(event.source);
    if (!id) {
      return;
    }
    if (message.type === 'linwidget_resize_widget') {
      applyWidgetGeometryRequest(id, message, false);
    } else if (message.type === 'linwidget_set_widget_geometry') {
      applyWidgetGeometryRequest(id, message, true);
    } else if (message.type === 'linwidget_set_resize_ui_visible') {
      applyResizeUiRequest(id, message);
    } else if (message.type === 'linwidget_open_settings') {
      post({ type: 'linwidget_open_settings', instanceId: id, widgetId: id });
    } else if (message.type === 'linwidget_focus_request') {
      post({ type: 'focus_request', instanceId: id, widgetId: id, focusSource: message.focusSource || '' });
    } else if (message.type === 'linwidget_keydown') {
      post(Object.assign({}, message, {
        type: 'host_keydown',
        instanceId: id,
        widgetId: id
      }));
    } else if (message.type === 'linwidget_system_info_request') {
      const record = widgets.get(id);
      if (!hasPermission(record, 'desktop')) {
        sendWidgetMessage(id, {
          type: 'linwidget_system_info_response',
          requestId: message.requestId || '',
          action: message.action || '',
          ok: false,
          error: 'Desktop permission is required.'
        });
        denyPermission(id, 'desktop');
        return;
      }
      post(Object.assign({}, message, {
        type: 'linwidget_system_info_request',
        instanceId: id,
        widgetId: id
      }));
    } else if (message.type === 'permission_denied') {
      denyPermission(id, message.permission || 'unknown', message.url || '');
    } else if (message.type === 'linwidget_desktop_api') {
      const record = widgets.get(id);
      if (!hasPermission(record, 'desktop')) {
        denyPermission(id, 'desktop');
      }
    } else if (message.type === 'linwidget_storage_api') {
      const record = widgets.get(id);
      if (!hasPermission(record, 'storage')) {
        denyPermission(id, 'storage');
      }
    }
  });

  window.addEventListener('dragover', event => {
    event.preventDefault();
    event.stopPropagation();
  }, true);
  window.addEventListener('drop', event => {
    event.preventDefault();
    event.stopPropagation();
  }, true);

  chrome.webview.addEventListener('message', event => {
    const message = event.data || {};
    switch (message.type) {
    case 'add_widget':
      addWidget(message);
      break;
    case 'remove_widget':
      removeWidget(message);
      break;
    case 'update_widget_geometry':
      updateWidgetGeometry(message);
      break;
    case 'update_host_options':
      updateHostOptions(message);
      break;
    case 'reload_widget':
      reloadWidget(message);
      break;
    case 'system_info_response':
      sendWidgetMessage(String(message.instanceId || message.widgetId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_system_info_response'
      }));
      break;
    case 'system_info_update':
      sendWidgetMessage(String(message.instanceId || message.widgetId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_system_info_update'
      }));
      break;
    default:
      break;
    }
  });

  post({ type: 'ready', widgetId: '__shared__' });
})();
