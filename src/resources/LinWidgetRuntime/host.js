(() => {
  const surface = document.getElementById('surface');
  const widgets = new Map();
  let active = null;
  let pendingGeometryFrame = 0;
  const pendingGeometryIds = new Set();

  const post = message => {
    try {
      chrome.webview.postMessage(message);
    } catch (_) {
    }
  };

  const clampNumber = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const clampRange = (value, min, max) => Math.min(Math.max(value, min), max);
  const surfaceBounds = () => ({
    width: Math.max(1, surface ? surface.clientWidth : window.innerWidth),
    height: Math.max(1, surface ? surface.clientHeight : window.innerHeight)
  });

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

  const startAction = (event, id) => {
    if (event.button !== 0) {
      return;
    }
    const record = widgets.get(id);
    if (!record) {
      return;
    }
    active = {
      id,
      startX: event.screenX,
      startY: event.screenY,
      startGeometry: geometryOf(record.panel)
    };
    document.body.classList.add('dragging');
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
      post({ type: 'console_log', level: 'info', message: `iframe loaded ${id}: ${iframe.src}` });
    });
    iframe.addEventListener('error', () => {
      post({ type: 'widget_error', widgetId: id, instanceId: id, message: `iframe failed to load: ${iframe.src}` });
    });

    const dragHandle = document.createElement('div');
    dragHandle.className = 'linwidget-drag-handle';

    const shield = document.createElement('div');
    shield.className = 'linwidget-drag-shield';

    const resizable = message.resizable !== false;

    dragHandle.addEventListener('pointerdown', event => startAction(event, id));

    panel.appendChild(iframe);
    panel.appendChild(dragHandle);
    panel.appendChild(shield);
    applyGeometry(panel, message.geometry || message, constraints);
    surface.appendChild(panel);
    widgets.set(id, { panel, iframe, constraints, resizable });
    post({ type: 'console_log', level: 'info', message: `added widget ${id}: ${iframe.src}` });
  };

  const removeWidget = message => {
    const id = String(message.instanceId || message.widgetId || '').trim();
    const record = widgets.get(id);
    if (!record) {
      return;
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
    const record = widgets.get(id);
    if (record) {
      record.iframe.src = record.iframe.src;
    }
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
    next.x += dx;
    next.y += dy;
    applyGeometry(record.panel, next, record.constraints);
    scheduleLiveGeometry(active.id);
    event.preventDefault();
  });

  window.addEventListener('pointerup', () => {
    if (!active) {
      return;
    }
    const id = active.id;
    active = null;
    document.body.classList.remove('dragging');
    notifyGeometry(id, false);
  });

  window.addEventListener('pointercancel', () => {
    active = null;
    document.body.classList.remove('dragging');
  });

  window.addEventListener('resize', () => reclampAllWidgets(true));

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
    default:
      break;
    }
  });

  post({ type: 'ready', widgetId: '__shared__' });
})();
