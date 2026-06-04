(() => {
  const surface = document.getElementById('surface');
  const widgets = new Map();
  let active = null;
  let pendingGeometryFrame = 0;
  const pendingGeometryIds = new Set();
  const loadTimeoutMs = 8000;
  const maxAutoReloadAttempts = 2;
  const resizeHitSize = 8;
  const snapThreshold = 8;
  const resizeCursorByMode = {
    s: 'ns-resize',
    e: 'ew-resize',
    ne: 'nesw-resize',
    sw: 'nesw-resize',
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

  let guideLayer = null;
  let verticalGuide = null;
  let horizontalGuide = null;

  const ensureGuideLayer = () => {
    if (guideLayer) {
      return guideLayer;
    }
    guideLayer = document.createElement('div');
    guideLayer.className = 'linwidget-alignment-guides';
    verticalGuide = document.createElement('div');
    verticalGuide.className = 'linwidget-alignment-guide linwidget-alignment-guide-vertical';
    horizontalGuide = document.createElement('div');
    horizontalGuide.className = 'linwidget-alignment-guide linwidget-alignment-guide-horizontal';
    guideLayer.appendChild(verticalGuide);
    guideLayer.appendChild(horizontalGuide);
    surface.appendChild(guideLayer);
    return guideLayer;
  };

  const setAlignmentGuides = guides => {
    ensureGuideLayer();
    const vertical = guides && guides.vertical && guides.vertical.visible;
    const horizontal = guides && guides.horizontal && guides.horizontal.visible;
    guideLayer.classList.toggle('visible', Boolean(vertical || horizontal));
    verticalGuide.classList.toggle('visible', Boolean(vertical));
    horizontalGuide.classList.toggle('visible', Boolean(horizontal));
    if (vertical) {
      verticalGuide.style.left = `${Math.round(clampNumber(guides.vertical.x, 0))}px`;
    }
    if (horizontal) {
      horizontalGuide.style.top = `${Math.round(clampNumber(guides.horizontal.y, 0))}px`;
    }
  };

  const clearAlignmentGuides = () => setAlignmentGuides({});

  const widgetContextMessage = (id, record) => ({
    type: 'linwidget_context',
    context: 'widget',
    instanceId: id,
    widgetId: record && record.manifestId ? record.manifestId : '',
    widgetName: record && record.name ? record.name : '',
    registryId: record && record.registryId ? record.registryId : '',
    multiInstance: Boolean(record && record.multiInstance),
    permissions: record && Array.isArray(record.permissions) ? record.permissions : []
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

  const setSharedDragBackground = enabled => {
    post({
      type: 'linwidget_drag_background',
      active: Boolean(enabled)
    });
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

  const edgePoints = geometry => ({
    left: geometry.x,
    centerX: geometry.x + geometry.width / 2,
    right: geometry.x + geometry.width,
    top: geometry.y,
    centerY: geometry.y + geometry.height / 2,
    bottom: geometry.y + geometry.height
  });

  const snapMoveGeometry = (id, geometry, disabled = false) => {
    if (disabled) {
      clearAlignmentGuides();
      return { geometry, guides: {} };
    }
    const moving = edgePoints(geometry);
    let bestX = null;
    let bestY = null;
    for (const [otherId, record] of widgets) {
      if (otherId === id || !record || !record.panel) {
        continue;
      }
      const targetGeometry = geometryOf(record.panel);
      const target = edgePoints(targetGeometry);
      const xPairs = [
        { source: moving.left, target: target.left, offset: 0 },
        { source: moving.left, target: target.centerX, offset: 0 },
        { source: moving.left, target: target.right, offset: 0 },
        { source: moving.centerX, target: target.left, offset: geometry.width / 2 },
        { source: moving.centerX, target: target.centerX, offset: geometry.width / 2 },
        { source: moving.centerX, target: target.right, offset: geometry.width / 2 },
        { source: moving.right, target: target.left, offset: geometry.width },
        { source: moving.right, target: target.centerX, offset: geometry.width },
        { source: moving.right, target: target.right, offset: geometry.width }
      ];
      const yPairs = [
        { source: moving.top, target: target.top, offset: 0 },
        { source: moving.top, target: target.centerY, offset: 0 },
        { source: moving.top, target: target.bottom, offset: 0 },
        { source: moving.centerY, target: target.top, offset: geometry.height / 2 },
        { source: moving.centerY, target: target.centerY, offset: geometry.height / 2 },
        { source: moving.centerY, target: target.bottom, offset: geometry.height / 2 },
        { source: moving.bottom, target: target.top, offset: geometry.height },
        { source: moving.bottom, target: target.centerY, offset: geometry.height },
        { source: moving.bottom, target: target.bottom, offset: geometry.height }
      ];
      for (const pair of xPairs) {
        const distance = Math.abs(pair.source - pair.target);
        if (distance <= snapThreshold && (!bestX || distance < bestX.distance)) {
          bestX = { distance, x: pair.target - pair.offset, guideX: pair.target };
        }
      }
      for (const pair of yPairs) {
        const distance = Math.abs(pair.source - pair.target);
        if (distance <= snapThreshold && (!bestY || distance < bestY.distance)) {
          bestY = { distance, y: pair.target - pair.offset, guideY: pair.target };
        }
      }
    }
    const snapped = Object.assign({}, geometry);
    const guides = {};
    if (bestX) {
      snapped.x = bestX.x;
      guides.vertical = { visible: true, x: bestX.guideX };
    }
    if (bestY) {
      snapped.y = bestY.y;
      guides.horizontal = { visible: true, y: bestY.guideY };
    }
    return { geometry: snapped, guides };
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
    if (!event.ctrlKey) {
      return '';
    }
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

  const resizeModeAtClientPoint = (record, x, y) => {
    if (!record || record.resizable === false || record.resizeUiVisible === false) {
      return '';
    }
    const geometry = geometryOf(record.panel);
    const nearLeft = x >= 0 && x <= resizeHitSize;
    const nearRight = x >= geometry.width - resizeHitSize && x <= geometry.width;
    const nearTop = y >= 0 && y <= resizeHitSize;
    const nearBottom = y >= geometry.height - resizeHitSize && y <= geometry.height;
    const vertical = nearTop ? 'n' : nearBottom ? 's' : '';
    const horizontal = nearLeft ? 'w' : nearRight ? 'e' : '';
    return vertical + horizontal;
  };

  const updateResizeHover = (record, event) => {
    if (!record || !event.ctrlKey || record.resizable === false || record.resizeUiVisible === false || active) {
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
    if (!event.ctrlKey) {
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
      dragX: 0,
      dragY: 0,
      startGeometry: geometryOf(record.panel)
    };
    document.body.classList.toggle('dragging', mode === 'move');
    document.body.classList.toggle('resizing', mode !== 'move');
    if (mode === 'move') {
      setSharedDragBackground(true);
    }
    record.panel.classList.toggle('active-move', mode === 'move');
    record.panel.classList.toggle('active-resize', mode !== 'move');
    try {
      event.currentTarget.setPointerCapture(event.pointerId);
    } catch (_) {
    }
    event.preventDefault();
  };

  const startWidgetAction = (id, mode = 'move') => {
    const record = widgets.get(id);
    if (!record) {
      return false;
    }
    active = {
      id,
      mode,
      startX: 0,
      startY: 0,
      dragX: 0,
      dragY: 0,
      startGeometry: geometryOf(record.panel)
    };
    document.body.classList.toggle('dragging', mode === 'move');
    document.body.classList.toggle('resizing', mode !== 'move');
    if (mode === 'move') {
      setSharedDragBackground(true);
    }
    record.panel.classList.toggle('active-move', mode === 'move');
    record.panel.classList.toggle('active-resize', mode !== 'move');
    record.panel.dataset.resizeMode = mode === 'move' ? '' : mode;
    record.panel.style.cursor = resizeCursorByMode[mode] || '';
    return true;
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
    const entryUrl = message.entryUrl || 'about:blank';
    try {
      const url = new URL(entryUrl);
      const granted = Array.isArray(message.permissions) ? message.permissions.join(',') : '';
      url.searchParams.set('__linwidget_permissions', granted);
      iframe.src = url.toString();
    } catch (_) {
      iframe.src = entryUrl;
    }
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

    const shield = document.createElement('div');
    shield.className = 'linwidget-drag-shield';

    const resizeFrame = document.createElement('div');
    resizeFrame.className = 'linwidget-resize-frame';
    const resizeModes = ['n', 's', 'e', 'w', 'nw', 'ne', 'sw', 'se'];

    const resizable = message.resizable !== false;

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
      if (event.target !== panel) {
        return;
      }
      const record = widgets.get(id);
      const mode = updateResizeHover(record, event);
      if (mode && record && record.resizable !== false && record.resizeUiVisible !== false) {
        startAction(event, id, mode);
      } else if (record && event.ctrlKey) {
        startAction(event, id, 'move');
      }
    }, true);
    const resizeHandles = resizeModes.map(mode => {
      const handle = document.createElement('div');
      handle.className = `linwidget-resize-hit linwidget-resize-${mode}`;
      handle.dataset.mode = mode;
      handle.style.cursor = '';
      handle.addEventListener('pointerenter', event => {
        const record = widgets.get(id);
        if (!record || record.resizable === false || record.resizeUiVisible === false || active) {
          return;
        }
        if (!event.ctrlKey) {
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
        if (!record || !event.ctrlKey || record.resizable === false || record.resizeUiVisible === false) {
          return;
        }
        record.panel.dataset.resizeMode = mode;
        record.panel.style.cursor = resizeCursorByMode[mode] || '';
        startAction(event, id, mode);
      });
      return handle;
    });

    panel.appendChild(iframe);
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
      registryId: String(message.registryId || message.manifestId || '').trim(),
      multiInstance: Boolean(message.multiInstance),
      accessToken: String(message.accessToken || '').trim(),
      name: String(message.name || '').trim(),
      permissions: Array.isArray(message.permissions) ? message.permissions : []
    });
    applyGeometry(panel, message.geometry || message, constraints);
    surface.appendChild(panel);
    const record = widgets.get(id);
    if (record) {
      record.loadTimer = setTimeout(() => recoverWidgetLoad(id, 'timeout'), loadTimeoutMs);
    }
    requestAnimationFrame(() => notifyGeometry(id, true));
    setTimeout(() => notifyGeometry(id, true), 50);
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
    clearAlignmentGuides();
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

  const applyWidgetDragMove = (id, message) => {
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    if (!active || active.id !== id) {
      startWidgetAction(id, 'move');
    }
    if (!active || active.id !== id) {
      return;
    }
    active.snapDisabled = Boolean(message.altKey);
    active.dragX += clampNumber(message.dx, 0);
    active.dragY += clampNumber(message.dy, 0);
    applyActiveGeometry(active.dragX, active.dragY);
    scheduleLiveGeometry(id);
  };

  const startWidgetDragFromIframe = (id, message) => {
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    const mode = resizeModeAtClientPoint(record, clampNumber(message.clientX, -1), clampNumber(message.clientY, -1)) || 'move';
    startWidgetAction(id, mode);
  };

  const finishWidgetDrag = id => {
    if (active && active.id === id) {
      const record = widgets.get(id);
      if (record) {
        record.panel.classList.remove('active-move');
        record.panel.classList.remove('active-resize');
        record.panel.dataset.resizeMode = '';
        record.panel.style.cursor = '';
      }
      active = null;
      document.body.classList.remove('dragging');
      document.body.classList.remove('resizing');
      setSharedDragBackground(false);
      clearAlignmentGuides();
    }
    if (widgets.has(id)) {
      notifyGeometry(id, false);
    }
  };

  const applyAlignmentGuideMessage = message => {
    setAlignmentGuides({
      vertical: message.vertical,
      horizontal: message.horizontal
    });
  };

  const applyActiveGeometry = (dx, dy) => {
    if (!active) {
      return;
    }
    const record = widgets.get(active.id);
    if (!record) {
      return;
    }
    const next = Object.assign({}, active.startGeometry);
    if (active.mode === 'move') {
      next.x += dx;
      next.y += dy;
      const snapped = snapMoveGeometry(active.id, next, Boolean(active.snapDisabled));
      next.x = snapped.geometry.x;
      next.y = snapped.geometry.y;
      setAlignmentGuides(snapped.guides);
    } else {
      clearAlignmentGuides();
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
    active.snapDisabled = Boolean(event.altKey);
    applyActiveGeometry(dx, dy);
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
    setSharedDragBackground(false);
    clearAlignmentGuides();
    if (record) {
      record.panel.classList.remove('active-move');
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
        record.panel.classList.remove('active-move');
        record.panel.classList.remove('active-resize');
        record.panel.dataset.resizeMode = '';
        record.panel.style.cursor = '';
      }
    }
    active = null;
    document.body.classList.remove('dragging');
    document.body.classList.remove('resizing');
    setSharedDragBackground(false);
    clearAlignmentGuides();
  });

  window.addEventListener('resize', () => reclampAllWidgets(true));

  const updateCtrlActive = event => {
    document.body.classList.toggle('ctrl-active', Boolean(event.ctrlKey));
  };

  window.addEventListener('keydown', event => {
    updateCtrlActive(event);
    post({
      type: 'host_keydown',
      key: event.key || '',
      code: event.code || '',
      target: describeTarget(event.target),
      defaultPrevented: Boolean(event.defaultPrevented)
    });
  }, true);

  window.addEventListener('keyup', event => updateCtrlActive(event), true);
  window.addEventListener('blur', () => document.body.classList.remove('ctrl-active'), true);

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
    } else if (message.type === 'linwidget_drag_start') {
      startWidgetDragFromIframe(id, message);
    } else if (message.type === 'linwidget_drag_move') {
      applyWidgetDragMove(id, message);
    } else if (message.type === 'linwidget_drag_end') {
      finishWidgetDrag(id);
    } else if (message.type === 'linwidget_keydown') {
      document.body.classList.toggle('ctrl-active', Boolean(message.ctrlKey));
      post(Object.assign({}, message, {
        type: 'host_keydown',
        instanceId: id,
        widgetId: id
      }));
    } else if (message.type === 'linwidget_keyup') {
      if (!message.ctrlKey || message.key === 'Control' || message.code === 'ControlLeft' || message.code === 'ControlRight') {
        document.body.classList.remove('ctrl-active');
      }
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
        widgetId: id,
        accessToken: record ? record.accessToken : ''
      }));
    } else if (message.type === 'linwidget_storage_request') {
      const record = widgets.get(id);
      if (!hasPermission(record, 'storage')) {
        sendWidgetMessage(id, {
          type: 'linwidget_storage_response',
          requestId: message.requestId || '',
          ok: false,
          error: 'Storage permission is required.'
        });
        denyPermission(id, 'storage');
        return;
      }
      post(Object.assign({}, message, {
        type: 'linwidget_storage_request',
        instanceId: id,
        widgetId: id,
        accessToken: record ? record.accessToken : ''
      }));
    } else if (message.type === 'linwidget_create_instance_request') {
      const record = widgets.get(id);
      if (!hasPermission(record, 'desktop')) {
        sendWidgetMessage(id, {
          type: 'linwidget_create_instance_response',
          requestId: message.requestId || '',
          ok: false,
          error: 'Desktop permission is required.'
        });
        denyPermission(id, 'desktop');
        return;
      }
      post(Object.assign({}, message, {
        type: 'linwidget_create_instance_request',
        instanceId: id,
        widgetId: id,
        accessToken: record ? record.accessToken : ''
      }));
    } else if (message.type === 'linwidget_list_instances_request') {
      const record = widgets.get(id);
      post(Object.assign({}, message, {
        type: 'linwidget_list_instances_request',
        instanceId: id,
        widgetId: id,
        accessToken: record ? record.accessToken : ''
      }));
    } else if (message.type === 'linwidget_close_instance_request') {
      const record = widgets.get(id);
      post({
        type: 'linwidget_close_instance_request',
        instanceId: id,
        widgetId: id,
        targetInstanceId: message.targetInstanceId || '',
        requestId: message.requestId || '',
        accessToken: record ? record.accessToken : ''
      });
    } else if (message.type === 'linwidget_remove_instance_request') {
      const record = widgets.get(id);
      post({
        type: 'linwidget_remove_instance_request',
        instanceId: id,
        widgetId: id,
        targetInstanceId: message.targetInstanceId || '',
        requestId: message.requestId || '',
        accessToken: record ? record.accessToken : ''
      });
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
    } else if (message.type && message.type.startsWith('linwidget_')) {
      post(Object.assign({}, message, {
        instanceId: id,
        widgetId: id
      }));
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
    case 'update_alignment_guides':
      applyAlignmentGuideMessage(message);
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
    case 'storage_response':
      sendWidgetMessage(String(message.instanceId || message.widgetId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_storage_response'
      }));
      break;
    case 'create_widget_instance_response':
      sendWidgetMessage(String(message.targetInstanceId || '').trim(), {
        type: 'linwidget_create_instance_response',
        requestId: message.requestId || '',
        ok: message.ok !== false,
        instanceId: message.instanceId || '',
        error: message.error || ''
      });
      break;
    case 'list_widget_instances_response':
      sendWidgetMessage(String(message.targetInstanceId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_list_instances_response'
      }));
      break;
    case 'close_widget_instance_response':
      sendWidgetMessage(String(message.targetInstanceId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_close_instance_response'
      }));
      break;
    case 'remove_widget_instance_response':
      sendWidgetMessage(String(message.targetInstanceId || '').trim(), Object.assign({}, message, {
        type: 'linwidget_remove_instance_response'
      }));
      break;
    default:
      if (message.type && message.type.startsWith('linwidget_')) {
        sendWidgetMessage(String(message.instanceId || message.widgetId || '').trim(), message);
      }
      break;
    }
  });

  post({ type: 'ready', widgetId: '__shared__' });
})();
