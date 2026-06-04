// ── Log helpers ──────────────────────────────────────────────────────────────
const logArea  = document.getElementById('logArea');
const logCount = document.getElementById('logCount');
let entryCount = 0;

function ts() {
  return new Date().toTimeString().slice(0, 8);
}

function appendLog(cls, ...parts) {
  const line = document.createElement('div');
  line.className = cls;
  line.textContent = `[${ts()}] ${parts.join(' ')}`;
  logArea.appendChild(line);
  logArea.scrollTop = logArea.scrollHeight;
  logCount.textContent = `${++entryCount} entries`;
}

const L = {
  head : (...a) => appendLog('e-head', ...a),
  info : (...a) => appendLog('e-info', ...a),
  ok   : (...a) => appendLog('e-ok',   '✓', ...a),
  err  : (...a) => appendLog('e-err',  '✗', ...a),
  warn : (...a) => appendLog('e-warn', '⚠', ...a),
  data : (...a) => appendLog('e-data', ...a),
  dim  : (...a) => appendLog('e-dim',  ...a),
};

function showResult(label, result) {
  if (result && result.ok) {
    L.ok(label);
    if (result.value !== undefined) {
      L.data(JSON.stringify(result.value, null, 2));
    }
  } else {
    L.err(label, '→', result ? result.error : 'null result');
  }
}

// ── Desktop ready ─────────────────────────────────────────────────────────────
function d() { return window.desktop || null; }

// ── Toolbar ───────────────────────────────────────────────────────────────────
document.getElementById('btnClear').onclick = () => {
  logArea.innerHTML = '';
  entryCount = 0;
  logCount.textContent = '0 entries';
};
document.getElementById('btnCopy').onclick = () => {
  navigator.clipboard.writeText(logArea.innerText).then(() => L.dim('Copied to clipboard'));
};

// ── Register button ───────────────────────────────────────────────────────────
function on(id, fn) {
  document.getElementById(id).addEventListener('click', async () => {
    try { await fn(); } catch (e) { L.err('Exception:', e.message); }
  });
}

// ── IDENTITY ──────────────────────────────────────────────────────────────────
on('t-identity', () => {
  L.head('── Identity ──');
  const dk = d();
  if (!dk) { L.err('desktop not available'); return; }
  L.info('context          :', dk.context);
  L.info('widgetId         :', dk.widgetId);
  L.info('widgetName       :', dk.widgetName);
  L.info('instanceId       :', dk.instanceId);
  L.info('instance.id      :', dk.instance?.id);
  L.info('instance.regId   :', dk.instance?.registryId);
  L.info('instance.multi   :', dk.instance?.multiInstance);
  document.getElementById('titleId').textContent = dk.instanceId?.split(':').pop() || '—';
});

// ── HOST CONTROLS ─────────────────────────────────────────────────────────────
on('t-resize', () => {
  L.head('── resizeWidget(500, 640) ──');
  const dk = d();
  if (!dk) { L.err('desktop not available'); return; }
  dk.resizeWidget({ width: 500, height: 640 });
  L.ok('called');
});

on('t-geometry', () => {
  L.head('── setWidgetGeometry ──');
  const dk = d();
  if (!dk) { L.err('desktop not available'); return; }
  dk.setWidgetGeometry({ x: 100, y: 100, width: 480, height: 620 });
  L.ok('called x=100 y=100 w=480 h=620');
});

on('t-resize-show', () => {
  L.head('── setResizeUiVisible(true) ──');
  d()?.setResizeUiVisible(true);
  L.ok('called');
});

on('t-resize-hide', () => {
  L.head('── setResizeUiVisible(false) ──');
  d()?.setResizeUiVisible(false);
  L.ok('called');
});

on('t-settings', () => {
  L.head('── openSettings() ──');
  const dk = d();
  if (dk && typeof dk.openSettings === 'function') {
    dk.openSettings();
    L.ok('called');
  } else {
    L.warn('openSettings not available');
  }
});

// ── MULTI INSTANCE ────────────────────────────────────────────────────────────
on('t-create', async () => {
  L.head('── instances.create() ──');
  const dk = d();
  L.info('desktop.instances =', dk?.instances ? 'ok' : 'undefined');
  L.info('instance.multiInstance =', String(dk?.instance?.multiInstance));
  L.info('instanceId =', dk?.instanceId);
  if (!dk?.instances?.create) { L.err('instances.create not available'); return; }
  try {
    const timeout = new Promise((_, rej) =>
      setTimeout(() => rej(new Error('超时：Promise 5秒未响应')), 5000));
    const r = await Promise.race([dk.instances.create(), timeout]);
    showResult('instances.create', r);
    if (r?.ok) L.info('new instanceId:', r.instanceId);
  } catch (e) {
    L.err('异常:', e.message);
  }
});

on('t-list', async () => {
  L.head('── instances.list() ──');
  const dk = d();
  if (!dk?.instances?.list) { L.err('instances.list not available'); return; }
  const r = await dk.instances.list();
  if (r?.ok) {
    L.ok(`${(r.instances||[]).length} instance(s)`);
    (r.instances||[]).forEach(item => {
      L.data(`  ${item.instanceId.split(':').pop()} running=${item.running} visible=${item.visible}  ${JSON.stringify(item.geometry||{})}`);
    });
  } else {
    L.err(r?.error);
  }
});

on('t-inst-close', async () => {
  L.head('── instances.close() ── (current)');
  const dk = d();
  if (!dk?.instances?.close) { L.err('instances.close not available'); return; }
  const r = await dk.instances.close();
  showResult('instances.close', r ?? { ok: true });
});

on('t-inst-remove', async () => {
  L.head('── instances.remove() ── (current)');
  const dk = d();
  if (!dk?.instances?.remove) { L.err('instances.remove not available'); return; }
  L.warn('This will remove the current instance!');
  const r = await dk.instances.remove();
  showResult('instances.remove', r ?? { ok: true });
});

on('t-instances-close', () => {
  L.head('── desktop.instances.close() ── (no-arg alias)');
  const dk = d();
  if (!dk?.instances?.close) { L.err('instances.close not available'); return; }
  dk.instances.close();
  L.ok('called');
});

// ── STORAGE · INSTANCE ───────────────────────────────────────────────────────
const TEST_KEY = 'apitester_key';
const TEST_VAL = { msg: 'hello', ts: Date.now() };

on('t-iset', async () => {
  L.head('── storage.instance.set ──');
  const dk = d();
  if (!dk?.storage?.instance) { L.err('storage.instance not available'); return; }
  const r = await dk.storage.instance.set(TEST_KEY, TEST_VAL);
  showResult('instance.set', r);
  L.info('key:', TEST_KEY, '→', JSON.stringify(TEST_VAL));
});

on('t-iget', async () => {
  L.head('── storage.instance.get ──');
  const dk = d();
  if (!dk?.storage?.instance) { L.err('storage.instance not available'); return; }
  const r = await dk.storage.instance.get(TEST_KEY);
  showResult('instance.get', r);
});

on('t-iremove', async () => {
  L.head('── storage.instance.remove ──');
  const dk = d();
  if (!dk?.storage?.instance) { L.err('storage.instance not available'); return; }
  const r = await dk.storage.instance.remove(TEST_KEY);
  showResult('instance.remove', r);
});

on('t-iclear', async () => {
  L.head('── storage.instance.clear ──');
  const dk = d();
  if (!dk?.storage?.instance) { L.err('storage.instance not available'); return; }
  L.warn('Clearing ALL instance storage!');
  const r = await dk.storage.instance.clear();
  showResult('instance.clear', r);
});

// ── STORAGE · WIDGET ─────────────────────────────────────────────────────────
on('t-wset', async () => {
  L.head('── storage.widget.set ──');
  const dk = d();
  if (!dk?.storage?.widget) { L.err('storage.widget not available'); return; }
  const r = await dk.storage.widget.set(TEST_KEY, TEST_VAL);
  showResult('widget.set', r);
});

on('t-wget', async () => {
  L.head('── storage.widget.get ──');
  const dk = d();
  if (!dk?.storage?.widget) { L.err('storage.widget not available'); return; }
  const r = await dk.storage.widget.get(TEST_KEY);
  showResult('widget.get', r);
});

on('t-wall', async () => {
  L.head('── storage.widget.all ──');
  const dk = d();
  if (!dk?.storage?.widget) { L.err('storage.widget not available'); return; }
  const r = await dk.storage.widget.all();
  showResult('widget.all', r);
});

on('t-wremove', async () => {
  L.head('── storage.widget.remove ──');
  const dk = d();
  if (!dk?.storage?.widget) { L.err('storage.widget not available'); return; }
  const r = await dk.storage.widget.remove(TEST_KEY);
  showResult('widget.remove', r);
});

// ── SYSTEM INFO ───────────────────────────────────────────────────────────────
async function sysCall(name, fn) {
  L.head(`── systemInfo.${name}() ──`);
  const dk = d();
  if (!dk?.systemInfo) { L.err('systemInfo not available'); return; }
  try {
    const r = await fn(dk.systemInfo);
    L.ok(name);
    L.data(JSON.stringify(r, null, 2));
  } catch (e) { L.err(name, e.message); }
}

on('t-snapshot', () => sysCall('getSnapshot', s => s.getSnapshot()));
on('t-cpu',      () => sysCall('cpu',         s => s.cpu()));
on('t-gpu',      () => sysCall('gpu',         s => s.gpu()));
on('t-mem',      () => sysCall('memory',      s => s.memory()));
on('t-disk',     () => sysCall('disk',        s => s.disk()));
on('t-net',      () => sysCall('network',     s => s.network()));
on('t-bat',      () => sysCall('battery',     s => s.battery()));
on('t-os',       () => sysCall('os',          s => s.os()));
on('t-uptime',   () => sysCall('uptime',      s => s.uptime()));

let watchId = null;
on('t-watch', () => {
  L.head('── systemInfo.watch(1000) ──');
  const dk = d();
  if (!dk?.systemInfo?.watch) { L.err('systemInfo.watch not available'); return; }
  if (watchId !== null) { L.warn('Already watching (id=' + watchId + '). Stop first.'); return; }
  watchId = dk.systemInfo.watch((info) => {
    L.info(`watch tick | cpu=${info.cpu?.usagePercent?.toFixed(1)}%`
      + ` mem=${info.memory?.usagePercent?.toFixed(1)}%`
      + ` net↓=${(info.network?.downloadBytesPerSecond/1024).toFixed(1)}KB/s`);
  }, 1000);
  L.ok('watching, id=' + watchId);
});

on('t-unwatch', async () => {
  L.head('── systemInfo.unwatch ──');
  const dk = d();
  if (!dk?.systemInfo?.unwatch) { L.err('systemInfo.unwatch not available'); return; }
  if (watchId === null) { L.warn('No active watch'); return; }
  await dk.systemInfo.unwatch(watchId);
  L.ok('unwatched id=' + watchId);
  watchId = null;
});

// ── NETWORK ───────────────────────────────────────────────────────────────────
on('t-fetch', async () => {
  L.head('── fetch(github API) ──');
  try {
    const r = await fetch('https://api.github.com/users/github');
    if (r.ok) {
      const j = await r.json();
      L.ok(`HTTP ${r.status}`);
      L.data(`login=${j.login}  repos=${j.public_repos}  followers=${j.followers}`);
    } else {
      L.err(`HTTP ${r.status}`);
    }
  } catch (e) { L.err(e.message); }
});

// ── LOCALSTORAGE ──────────────────────────────────────────────────────────────
on('t-ls-set', () => {
  L.head('── localStorage.setItem ──');
  try {
    localStorage.setItem('apitester_ls', JSON.stringify({ t: Date.now() }));
    L.ok('set apitester_ls =', localStorage.getItem('apitester_ls'));
  } catch (e) { L.err(e.message); }
});

on('t-ls-get', () => {
  L.head('── localStorage.getItem ──');
  const v = localStorage.getItem('apitester_ls');
  if (v !== null) { L.ok('value:', v); }
  else { L.warn('key not found'); }
});

on('t-ls-clear', () => {
  L.head('── localStorage.clear ──');
  try {
    localStorage.clear();
    L.ok('cleared');
  } catch (e) { L.err(e.message); }
});

// ── Storage event log ─────────────────────────────────────────────────────────
window.addEventListener('storage', e => {
  L.dim(`[storage event] key="${e.key}" new="${e.newValue}"`);
});

// ── Boot ──────────────────────────────────────────────────────────────────────
L.head('═══ LinWidget API Tester v2 ═══');
L.dim('desktop=' + (window.desktop ? 'ok' : 'MISSING'));
if (d()) {
  L.dim('context=' + d().context);
  L.dim('instanceId=' + d().instanceId);
  L.dim('permissions: desktop + storage + network');
  document.getElementById('titleId').textContent = d().instanceId?.split(':').pop() || '—';
}
L.dim('Click any button in the sidebar to run a test.');
