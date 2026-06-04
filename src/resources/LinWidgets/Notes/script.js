const noteArea = document.getElementById('noteArea');
const settingsBtn = document.getElementById('settingsBtn');
const clearNoteBtn = document.getElementById('clearNoteBtn');
const createInstanceBtn = document.getElementById('createInstanceBtn');
const closeInstanceBtn = document.getElementById('closeInstanceBtn');
const confirmDialog = document.getElementById('confirmDialog');
const confirmCancel = document.getElementById('confirmCancel');
const confirmYes = document.getElementById('confirmYes');

let saveTimeout = 0;
let currentInstanceId = '';

function log(...args) {
  const id = currentInstanceId ? currentInstanceId.split(':').pop() : '?';
  console.log(`[Notes:${id}]`, ...args);
}

function whenDesktopReady() {
  if (window.desktop && desktop.instanceId) {
    return Promise.resolve(desktop);
  }
  if (window.desktop && desktop.ready && typeof desktop.ready.then === 'function') {
    return desktop.ready;
  }
  return new Promise(resolve => {
    const done = () => resolve(window.desktop || null);
    window.addEventListener('linwidgetready', done, { once: true });
    setTimeout(done, 3000);
  });
}

async function storageGet(scope, key, fallbackValue = '') {
  if (scope === 'instance') {
    const instanceKey = `lin_sticky_notes_instance_${currentInstanceId || 'default'}_${key}`;
    const val = localStorage.getItem(instanceKey);
    if (val !== null) return val;
    if (key === 'theme' || key === 'fontsize') {
      const globalVal = localStorage.getItem(`lin_sticky_notes_${key}`);
      if (globalVal !== null) return globalVal;
    }
    return fallbackValue;
  }
  return localStorage.getItem(`lin_sticky_notes_${key}`) ?? fallbackValue;
}

async function storageSet(scope, key, value) {
  if (scope === 'instance') {
    const instanceKey = `lin_sticky_notes_instance_${currentInstanceId || 'default'}_${key}`;
    localStorage.setItem(instanceKey, String(value));
    return true;
  }
  localStorage.setItem(`lin_sticky_notes_${key}`, String(value));
  return true;
}

function applyOpacity(opacityPercent) {
  const glassPanel = document.querySelector('.glass-panel');
  if (glassPanel) {
    glassPanel.style.setProperty('--bg-opacity', String(parseFloat(opacityPercent) / 100));
    glassPanel.style.opacity = '1'; // Ensure container itself stays fully opaque
  }
}

async function loadAppearance() {
  const theme = await storageGet('instance', 'theme', 'yellow');
  document.body.className = `theme-${theme || 'yellow'}`;

  const fontSize = await storageGet('instance', 'fontsize', '');
  if (fontSize) {
    noteArea.style.fontSize = fontSize;
  }

  const opacity = await storageGet('widget', 'opacity', '100');
  applyOpacity(opacity || '100');
}

async function loadNoteContent() {
  let content = await storageGet('instance', 'content', '');
  if (typeof content !== 'string') {
    content = '';
  }
  // Backward compatibility: convert old plain text with newlines to HTML
  if (content && !content.includes('<') && !content.includes('>')) {
    content = content.replace(/\n/g, '<br>');
  }
  noteArea.innerHTML = content;
}

function notifySettingsChanged() {
  try {
    if (window.chrome && chrome.webview && typeof chrome.webview.postMessage === 'function') {
      chrome.webview.postMessage({ type: 'linwidget_settings_changed_request' });
    }
  } catch (_) {}
}

async function saveNoteContent() {
  let html = noteArea.innerHTML;
  if (html === '<br>' || html === '<div><br></div>') {
    html = '';
  }
  await storageSet('instance', 'content', html);
  notifySettingsChanged();
}

function insertImageAtCursor(dataUrl) {
  noteArea.focus();
  const selection = window.getSelection();
  if (!selection.rangeCount) return;
  
  const range = selection.getRangeAt(0);
  range.deleteContents();
  
  const img = document.createElement('img');
  img.src = dataUrl;
  
  range.insertNode(img);
  // Move cursor to after the image
  range.setStartAfter(img);
  range.setEndAfter(img);
  selection.removeAllRanges();
  selection.addRange(range);
  
  scheduleNoteSave();
}

function compressAndInsertImage(file) {
  const reader = new FileReader();
  reader.onload = (event) => {
    const img = new Image();
    img.onload = () => {
      // Compress to maximum dimension of 1024px to look sharp but lightweight
      const maxDim = 1024;
      let width = img.width;
      let height = img.height;
      if (width > maxDim || height > maxDim) {
        if (width > height) {
          height = Math.round((height * maxDim) / width);
          width = maxDim;
        } else {
          width = Math.round((width * maxDim) / height);
          height = maxDim;
        }
      }
      
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0, width, height);
      
      // Export as JPG to optimize local storage size (0.75 quality)
      const compressedDataUrl = canvas.toDataURL('image/jpeg', 0.75);
      insertImageAtCursor(compressedDataUrl);
    };
    img.src = event.target.result;
  };
  reader.readAsDataURL(file);
}

async function saveImageToLocal(dataUrl) {
  try {
    if (typeof window.showSaveFilePicker === 'function') {
      const response = await fetch(dataUrl);
      const blob = await response.blob();
      
      const options = {
        suggestedName: `note_image_${Date.now()}.jpg`,
        types: [{
          description: 'JPEG Image',
          accept: {
            'image/jpeg': ['.jpg', '.jpeg']
          }
        }]
      };
      
      const handle = await window.showSaveFilePicker(options);
      const writable = await handle.createWritable();
      await writable.write(blob);
      await writable.close();
      return;
    }
  } catch (err) {
    if (err.name === 'AbortError') {
      return; // User cancelled the save dialog
    }
    console.error('File picker save failed:', err);
  }
  
  // Fallback to default browser download behavior
  const link = document.createElement('a');
  link.href = dataUrl;
  link.download = `note_image_${Date.now()}.jpg`;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

let activeContextMenu = null;
function removeContextMenu() {
  if (activeContextMenu) {
    activeContextMenu.remove();
    activeContextMenu = null;
  }
}

function scheduleNoteSave() {
  clearTimeout(saveTimeout);
  saveTimeout = setTimeout(saveNoteContent, 300);
}

async function createNewInstance() {
  await whenDesktopReady();
  if (!window.desktop || !desktop.instances || typeof desktop.instances.create !== 'function') {
    log('desktop.instances.create unavailable');
    return;
  }

  createInstanceBtn.disabled = true;
  try {
    log('instances.create start');
    const result = await desktop.instances.create();
    log('instances.create result:', JSON.stringify(result));
    if (!result || !result.ok) {
      console.warn('[Notes] failed to create note instance:', result ? result.error : 'unknown error');
    }
  } catch (error) {
    console.error('[Notes] createNewInstance failed', error);
  } finally {
    createInstanceBtn.disabled = false;
  }
}

async function closeCurrentInstance() {
  await saveNoteContent();
  if (window.desktop && desktop.instance && typeof desktop.instance.close === 'function') {
    await desktop.instance.close();
  } else if (window.desktop && desktop.instances && typeof desktop.instances.close === 'function') {
    await desktop.instances.close(currentInstanceId);
  }
}

function openSettings() {
  if (window.desktop && typeof desktop.openSettings === 'function') {
    desktop.openSettings();
  }
}

function showClearDialog() {
  confirmDialog.classList.remove('hidden');
}

function hideClearDialog() {
  confirmDialog.classList.add('hidden');
}

async function clearCurrentNote() {
  noteArea.innerHTML = '';
  await storageSet('instance', 'content', '');
  notifySettingsChanged();
}

function installEvents() {
  noteArea.addEventListener('input', scheduleNoteSave);

  noteArea.addEventListener('paste', (e) => {
    const items = (e.clipboardData || e.originalEvent.clipboardData).items;
    for (const item of items) {
      if (item.type.indexOf('image') === 0) {
        e.preventDefault();
        const file = item.getAsFile();
        compressAndInsertImage(file);
        break;
      }
    }
  });

  noteArea.addEventListener('dragover', (e) => {
    e.preventDefault();
  });

  noteArea.addEventListener('drop', (e) => {
    const files = e.dataTransfer.files;
    if (files.length > 0) {
      const file = files[0];
      if (file.type.indexOf('image') === 0) {
        e.preventDefault();
        compressAndInsertImage(file);
      }
    }
  });

  noteArea.addEventListener('contextmenu', (e) => {
    const target = e.target;
    if (target && target.tagName === 'IMG') {
      e.preventDefault();
      e.stopPropagation();
      removeContextMenu();
      
      const menu = document.createElement('div');
      menu.className = 'custom-context-menu';
      menu.style.left = `${e.clientX}px`;
      menu.style.top = `${e.clientY}px`;
      
      const saveOpt = document.createElement('div');
      saveOpt.className = 'custom-context-menu-item';
      saveOpt.innerHTML = `
        <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display:inline-block; vertical-align:middle; margin-right:4px;">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
          <polyline points="7 10 12 15 17 10"></polyline>
          <line x1="12" y1="15" x2="12" y2="3"></line>
        </svg>
        <span>保存图片到本地</span>
      `;
      saveOpt.addEventListener('click', () => {
        saveImageToLocal(target.src);
        removeContextMenu();
      });
      
      const deleteOpt = document.createElement('div');
      deleteOpt.className = 'custom-context-menu-item danger';
      deleteOpt.innerHTML = `
        <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display:inline-block; vertical-align:middle; margin-right:4px;">
          <polyline points="3 6 5 6 21 6"></polyline>
          <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
        </svg>
        <span>删除图片</span>
      `;
      deleteOpt.addEventListener('click', () => {
        target.remove();
        scheduleNoteSave();
        removeContextMenu();
      });
      
      menu.appendChild(saveOpt);
      menu.appendChild(deleteOpt);
      document.body.appendChild(menu);
      activeContextMenu = menu;
    }
  });

  document.addEventListener('click', removeContextMenu);
  document.addEventListener('contextmenu', (e) => {
    if (e.target.tagName !== 'IMG') {
      removeContextMenu();
    }
  });

  noteArea.addEventListener('wheel', event => {
    const zoomKeySetting = localStorage.getItem('lin_sticky_notes_zoom_key') || 'all';
    if (zoomKeySetting === 'none') {
      return;
    }

    let shouldZoom = false;
    if (zoomKeySetting === 'all') {
      shouldZoom = event.ctrlKey || event.altKey || event.shiftKey;
    } else if (zoomKeySetting === 'ctrl') {
      shouldZoom = event.ctrlKey;
    } else if (zoomKeySetting === 'alt') {
      shouldZoom = event.altKey;
    } else if (zoomKeySetting === 'shift') {
      shouldZoom = event.shiftKey;
    }

    if (!shouldZoom) {
      return;
    }
    event.preventDefault();
    const currentSize = parseFloat(window.getComputedStyle(noteArea).fontSize) || 15;
    const delta = event.deltaY < 0 ? 1 : -1;
    const newSize = Math.max(10, Math.min(60, currentSize + delta));
    noteArea.style.fontSize = `${newSize}px`;
    storageSet('instance', 'fontsize', `${newSize}px`);
    notifySettingsChanged();
  }, { passive: false });

  settingsBtn.addEventListener('click', openSettings);
  clearNoteBtn.addEventListener('click', async () => {
    const confirmClear = localStorage.getItem('lin_sticky_notes_confirm_clear') !== 'false';
    if (confirmClear) {
      showClearDialog();
    } else {
      await clearCurrentNote();
    }
  });
  confirmCancel.addEventListener('click', hideClearDialog);
  confirmYes.addEventListener('click', async () => {
    await clearCurrentNote();
    hideClearDialog();
  });
  createInstanceBtn.addEventListener('click', createNewInstance);
  closeInstanceBtn.addEventListener('click', closeCurrentInstance);

  window.addEventListener('storage', (e) => {
    if (e.key && e.key.startsWith('lin_sticky_notes_')) {
      loadAppearance();
    }
  });

  window.addEventListener('message', (e) => {
    const data = e.data || {};
    if (data.type === 'linwidget_appearance_changed_notification') {
      setTimeout(loadAppearance, 50);
    }
  });
}

async function boot() {
  installEvents();
  await whenDesktopReady();
  currentInstanceId = window.desktop && desktop.instanceId ? desktop.instanceId : '';
  log('desktop ready, multiInstance=', desktop.instance && desktop.instance.multiInstance);
  await loadAppearance();
  await loadNoteContent();
}

boot().catch(error => {
  console.error('[Notes] boot failed', error);
});
