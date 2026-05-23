const cpuRing = document.getElementById('cpuRing');
const cpuValue = document.getElementById('cpuValue');
const cpuDetail = document.getElementById('cpuDetail');

const ramRing = document.getElementById('ramRing');
const ramValue = document.getElementById('ramValue');
const ramDetail = document.getElementById('ramDetail');

const gpuValue = document.getElementById('gpuValue');
const gpuBar = document.getElementById('gpuBar');
const gpuVramLabel = document.getElementById('gpuVramLabel');
const gpuName = document.getElementById('gpuName');

const batBar = document.getElementById('batBar');
const batValue = document.getElementById('batValue');
const batDetail = document.getElementById('batDetail');
const chargeIcon = document.getElementById('chargeIcon');

const netDown = document.getElementById('netDown');
const netUp = document.getElementById('netUp');
const netCanvas = document.getElementById('netCanvas');
let ctx = null;

const netDown2 = document.getElementById('netDown2');
const netUp2 = document.getElementById('netUp2');
const netTotalDown = document.getElementById('netTotalDown');
const netTotalUp = document.getElementById('netTotalUp');
const netCanvas2 = document.getElementById('netCanvas2');
let ctx2 = null;

// New Details Panels
const cpuDetailList = document.getElementById('cpuDetailList');
const memDetailList = document.getElementById('memDetailList');
const diskDetailList = document.getElementById('diskDetailList');

// Micro Mode
const microCpuValue = document.getElementById('microCpuValue');
const microCpuRing = document.getElementById('microCpuRing');
const microRamValue = document.getElementById('microRamValue');
const microRamRing = document.getElementById('microRamRing');
const microGpuValue = document.getElementById('microGpuValue');
const microGpuRing = document.getElementById('microGpuRing');
const microBatValue = document.getElementById('microBatValue');
const microBatRing = document.getElementById('microBatRing');

// Style Switcher Logic
const styleDots = document.querySelectorAll('.style-dot');
const panelSections = document.querySelectorAll('.panel-section');
let currentStyleIndex = 0;

function switchPanel(index) {
  if (index < 0) index = 0;
  if (index >= styleDots.length) index = styleDots.length - 1;
  currentStyleIndex = index;
  
  styleDots.forEach(d => d.classList.remove('active'));
  panelSections.forEach(p => p.classList.remove('active'));
  
  const dot = styleDots[index];
  dot.classList.add('active');
  
  const styleId = dot.getAttribute('data-style');
  const targetPanel = document.getElementById(`panelStyle${styleId}`);
  if (targetPanel) {
    targetPanel.classList.add('active');
  }
  
  // Force redraw network graph if returning to panel 1 or 4
  if (styleId === '1') {
    setTimeout(() => fixCanvasResolution(netCanvas, ctx), 50);
  } else if (styleId === '4') {
    setTimeout(() => fixCanvasResolution(netCanvas2, ctx2), 50);
  }
}

styleDots.forEach((dot, index) => {
  dot.addEventListener('click', () => {
    switchPanel(index);
  });
});

let wheelCooldown = false;
window.addEventListener('wheel', (e) => {
  if (wheelCooldown) return;
  if (e.deltaY > 0) {
    if (currentStyleIndex < styleDots.length - 1) {
      switchPanel(currentStyleIndex + 1);
      triggerCooldown();
    }
  } else if (e.deltaY < 0) {
    if (currentStyleIndex > 0) {
      switchPanel(currentStyleIndex - 1);
      triggerCooldown();
    }
  }
});

function triggerCooldown() {
  wheelCooldown = true;
  setTimeout(() => { wheelCooldown = false; }, 300); // 300ms cooldown to avoid skipping
}

// Ring radius = 40, Circumference = 2 * PI * 40 ≈ 251.2
const RING_CIRCUMFERENCE = 251.2;

function setRing(element, percent, disableRed = false) {
  const offset = RING_CIRCUMFERENCE - (percent / 100) * RING_CIRCUMFERENCE;
  element.style.strokeDashoffset = offset;
  
  if (!disableRed && percent > 85) {
    element.style.stroke = 'var(--accent-red)';
  } else {
    element.style.stroke = '';
  }
}

function formatBytes(bytes, decimals = 1) {
  if (!+bytes) return '0 B';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`;
}

// Network Graph logic
const historyLength = 40; // amount of data points
const downHistory = new Array(historyLength).fill(0);
const upHistory = new Array(historyLength).fill(0);

function drawNetworkGraph() {
  const drawOnCtx = (canvasCtx, canvasEl) => {
    if (!canvasCtx || !canvasEl) return;
    const width = canvasEl.width;
    const height = canvasEl.height;
    canvasCtx.clearRect(0, 0, width, height);

    const maxVal = Math.max(...downHistory, ...upHistory, 1024 * 100); 
    
    function drawLine(data, color, fillRgba) {
      canvasCtx.beginPath();
      canvasCtx.strokeStyle = color;
      canvasCtx.lineWidth = 2 * window.devicePixelRatio;
      canvasCtx.lineJoin = 'round';
      
      for(let i = 0; i < data.length; i++) {
        const x = (i / (historyLength - 1)) * width;
        const y = height - (data[i] / maxVal) * height * 0.9 - (2 * window.devicePixelRatio);
        if (i === 0) canvasCtx.moveTo(x, y);
        else canvasCtx.lineTo(x, y);
      }
      canvasCtx.stroke();
      
      canvasCtx.lineTo(width, height);
      canvasCtx.lineTo(0, height);
      canvasCtx.fillStyle = fillRgba;
      canvasCtx.fill();
    }

    drawLine(upHistory, '#8b5cf6', 'rgba(139, 92, 246, 0.15)');
    drawLine(downHistory, '#10b981', 'rgba(16, 185, 129, 0.2)');
  };

  drawOnCtx(ctx, netCanvas);
  drawOnCtx(ctx2, netCanvas2);
}

function fixCanvasResolution(canvasEl, ctxRef) {
  if (!canvasEl) return;
  const rect = canvasEl.getBoundingClientRect();
  if (rect.width === 0) return;
  canvasEl.width = rect.width * window.devicePixelRatio;
  canvasEl.height = rect.height * window.devicePixelRatio;
  if (canvasEl === netCanvas) ctx = canvasEl.getContext('2d');
  if (canvasEl === netCanvas2) ctx2 = canvasEl.getContext('2d');
  drawNetworkGraph();
}

window.addEventListener('resize', () => {
  fixCanvasResolution(netCanvas, ctx);
  fixCanvasResolution(netCanvas2, ctx2);
});

let watchId = null;

async function startMonitoring() {
  if (!window.desktop || !desktop.systemInfo) {
    console.error("Desktop SystemInfo API not available.");
    return;
  }

  // Ensure canvas is ready
  setTimeout(() => {
    fixCanvasResolution(netCanvas, ctx);
    fixCanvasResolution(netCanvas2, ctx2);
  }, 100);

  console.log("Calling desktop.systemInfo.watch...");
  
  const handleSystemInfo = (rawInfo) => {
    let info = rawInfo;
    if (typeof rawInfo === 'string') {
      try {
        info = JSON.parse(rawInfo);
      } catch (e) {
        console.error("JSON parse error in SysMonitor:", e);
        return;
      }
    }
    console.log("SysMonitor tick received:", info);
    try {
        if (!info) {
          console.warn("Received empty info object");
          return;
        }

        // CPU
        if (info.cpu) {
          const cpuP = Math.round(info.cpu.usagePercent || 0);
          cpuValue.innerHTML = `${cpuP}<span class="percent">%</span>`;
          setRing(cpuRing, cpuP);
          cpuDetail.textContent = info.cpu.name || '?';
          cpuDetail.title = info.cpu.name || ''; // add title for full name on hover
        }

        // RAM
        if (info.memory) {
          const ramP = Math.round(info.memory.usagePercent || 0);
          ramValue.innerHTML = `${ramP}<span class="percent">%</span>`;
          setRing(ramRing, ramP);
          const totalRam = formatBytes(info.memory.totalBytes || 0, 0);
          const usedRam = formatBytes(info.memory.usedBytes || 0, 1);
          
          if (info.memory.frequencyMHz && info.memory.frequencyMHz > 0) {
            ramDetail.textContent = `${totalRam} @ ${info.memory.frequencyMHz} MHz`;
          } else {
            ramDetail.textContent = `${usedRam} / ${totalRam}`;
          }
        }

        // GPU
        if (info.gpu) {
          gpuName.textContent = info.gpu.name || 'Unknown GPU';
          gpuName.title = info.gpu.name || '';
          
          if (info.gpu.usagePercent !== undefined) {
             const gpuP = Math.round(info.gpu.usagePercent);
             gpuValue.textContent = `${gpuP}%`;
             gpuBar.style.width = `${gpuP}%`;
             if (gpuP > 90) gpuBar.style.background = 'var(--accent-red)';
             else gpuBar.style.background = 'var(--accent-purple)';
          }

          gpuVramLabel.style.display = 'inline-block';
          if (info.gpu.dedicatedVideoMemoryBytes > 0) {
            gpuVramLabel.textContent = formatBytes(info.gpu.dedicatedVideoMemoryBytes, 0);
          } else if (info.gpu.sharedSystemMemoryBytes > 0) {
            gpuVramLabel.textContent = formatBytes(info.gpu.sharedSystemMemoryBytes, 0);
          } else {
            gpuVramLabel.style.display = 'none';
          }
        }

        // Battery
        if (info.battery && info.battery.available) {
          const batP = info.battery.percent || 0;
          batValue.textContent = `${batP}%`;
          batBar.style.width = `${batP}%`;
          
          if (info.battery.charging || info.battery.acLineOnline) {
            chargeIcon.style.display = 'inline-block';
            batDetail.textContent = "AC Power";
            batBar.style.background = 'var(--accent-cyan)';
          } else {
            chargeIcon.style.display = 'none';
            batBar.style.background = batP < 20 ? 'var(--accent-red)' : 'var(--accent-green)';
            const life = info.battery.lifeSeconds || 0;
            const hrs = Math.floor(life / 3600);
            const mins = Math.floor((life % 3600) / 60);
            batDetail.textContent = life > 0 && life < 86400 
                ? `${hrs}h ${mins}m left` 
                : `On Battery`;
          }
        } else {
          batValue.textContent = "AC";
          batBar.style.width = `100%`;
          batBar.style.background = 'var(--accent-cyan)';
          batDetail.textContent = "Desktop PC";
        }

        // Network
        if (info.network) {
          const downBps = info.network.downloadBytesPerSecond || 0;
          const upBps = info.network.uploadBytesPerSecond || 0;
          netDown.textContent = formatBytes(downBps) + '/s';
          netUp.textContent = formatBytes(upBps) + '/s';
          if (netDown2) netDown2.textContent = formatBytes(downBps) + '/s';
          if (netUp2) netUp2.textContent = formatBytes(upBps) + '/s';
          if (netTotalDown) netTotalDown.textContent = formatBytes(info.network.totalReceivedBytes || 0, 1);
          if (netTotalUp) netTotalUp.textContent = formatBytes(info.network.totalSentBytes || 0, 1);

          // Update graph data
          downHistory.shift();
          downHistory.push(downBps);
          upHistory.shift();
          upHistory.push(upBps);
          
          if (ctx || ctx2) drawNetworkGraph();
        }

        // Update Panel 2 (CPU/Memory Details)
        if (info.cpu && cpuDetailList) {
          const cpuP = Math.round(info.cpu.usagePercent || 0);
          cpuDetailList.innerHTML = `
            <div class="detail-item">
              <span class="detail-label">Model</span>
              <span class="detail-val" title="${info.cpu.name || '?'}">${info.cpu.name || '?'}</span>
            </div>
            <div class="detail-item">
              <span class="detail-label">Cores</span>
              <span class="detail-val">${info.cpu.physicalCores || 0} P / ${info.cpu.logicalCores || 0} L</span>
            </div>
            <div class="detail-item" style="border:none; padding:0; flex-direction:column; gap:0.2rem; margin-top:0.2rem;">
              <div style="display:flex; justify-content:space-between;">
                <span class="detail-label">Usage</span>
                <span class="detail-val">${cpuP} %</span>
              </div>
              <div class="progress-bar">
                <div class="progress-fill" style="width: ${cpuP}%; background: ${cpuP > 90 ? 'var(--accent-red)' : 'var(--accent-cyan)'};"></div>
              </div>
            </div>
          `;
        }

        if (info.memory && memDetailList) {
          const memP = Math.round(info.memory.usagePercent || 0);
          let memHtml = `
            <div class="detail-item" style="border:none; padding:0; flex-direction:column; gap:0.2rem; margin-bottom:0.4rem;">
              <div style="display:flex; justify-content:space-between;">
                <span class="detail-label">RAM Usage</span>
                <span class="detail-val">${memP} %</span>
              </div>
              <div class="progress-bar">
                <div class="progress-fill" style="width: ${memP}%; background: ${memP > 90 ? 'var(--accent-red)' : 'var(--accent-purple)'};"></div>
              </div>
            </div>
            <div class="detail-item">
              <span class="detail-label">Total Size</span>
              <span class="detail-val">${formatBytes(info.memory.totalBytes, 1)}</span>
            </div>
            <div class="detail-item">
              <span class="detail-label">In Use</span>
              <span class="detail-val">${formatBytes(info.memory.usedBytes, 1)}</span>
            </div>
            <div class="detail-item">
              <span class="detail-label">Speed</span>
              <span class="detail-val">${info.memory.frequencyMHz || '?'} MHz</span>
            </div>
          `;
          if (info.memory.modules && info.memory.modules.length > 0) {
             memHtml += info.memory.modules.map(mod => `
                <div class="detail-item">
                  <span class="detail-label" title="${mod.locator}">${mod.locator || 'DIMM'}</span>
                  <span class="detail-val">${formatBytes(mod.sizeBytes, 1)} @ ${mod.speedMHz || mod.frequencyMHz}MHz</span>
                </div>
             `).join('');
          }
          memDetailList.innerHTML = memHtml;
        }

        // Update Panel 3 (Disk Details)
        if (info.disk && info.disk.drives && diskDetailList) {
          diskDetailList.innerHTML = info.disk.drives.map(drive => {
             const total = drive.totalBytes || 1;
             const free = drive.freeBytes || 0;
             const used = drive.usedBytes !== undefined ? drive.usedBytes : (total - free);
             const diskP = Math.round((used / total) * 100) || 0;
             const barColor = diskP > 90 ? 'var(--accent-red)' : 'var(--accent-blue)';
             return `
              <div class="disk-drive">
                <div class="disk-header">
                  <span class="detail-label">Drive ${drive.root}</span>
                  <span class="detail-val">${diskP}% Used</span>
                </div>
                <div class="progress-bar" style="margin-bottom: 0.3rem; height: 8px; background: rgba(0,0,0,0.1);">
                  <div class="progress-fill" style="width: ${diskP}%; background: ${barColor};"></div>
                </div>
                <div class="detail-item" style="border: none; padding: 0;">
                  <span class="detail-label" style="font-size:0.65rem;">Free: ${formatBytes(free, 1)}</span>
                  <span class="detail-label" style="font-size:0.65rem;">Total: ${formatBytes(total, 1)}</span>
                </div>
              </div>
             `;
          }).join('');
        }

        // Update Micro Mode
        if (info.cpu) {
          const p = Math.round(info.cpu.usagePercent || 0);
          if (microCpuValue) microCpuValue.innerHTML = `${p}<span class="percent">%</span>`;
          if (microCpuRing) setRing(microCpuRing, p);
        }
        if (info.memory) {
          const p = Math.round(info.memory.usagePercent || 0);
          if (microRamValue) microRamValue.innerHTML = `${p}<span class="percent">%</span>`;
          if (microRamRing) setRing(microRamRing, p);
        }
        if (info.gpu) {
          const p = Math.round(info.gpu.usagePercent || 0);
          if (microGpuValue) microGpuValue.innerHTML = `${p}<span class="percent">%</span>`;
          if (microGpuRing) setRing(microGpuRing, p);
        }
        if (info.battery && info.battery.available) {
          const life = info.battery.percent || 0;
          if (microBatValue) microBatValue.innerHTML = `${life}<span class="percent">%</span>`;
          if (microBatRing) setRing(microBatRing, life, true);
        } else {
          if (microBatValue) microBatValue.innerHTML = `AC`;
          if (microBatRing) setRing(microBatRing, 100, true);
        }

      } catch (err) {
        console.error("Error updating SysMonitor UI:", err);
      }
  };

  try {
    let hasReceivedTick = false;
    watchId = desktop.systemInfo.watch(info => {
      hasReceivedTick = true;
      handleSystemInfo(info);
    }, 1000);
    console.log("Watch started with ID:", watchId);

    // Fallback: If watch doesn't fire within 2.5 seconds, use setInterval + getSnapshot
    setTimeout(() => {
      if (!hasReceivedTick) {
        console.warn("Watch callback never fired! Falling back to setInterval + getSnapshot...");
        if (watchId) desktop.systemInfo.unwatch(watchId).catch(() => {});
        
        setInterval(async () => {
          try {
            const snap = await desktop.systemInfo.getSnapshot();
            handleSystemInfo(snap);
          } catch (e) {
            console.error("Fallback getSnapshot error:", e);
          }
        }, 1000);
      }
    }, 2500);

  } catch (err) {
    console.error("Error calling systemInfo.watch:", err);
  }
}

startMonitoring();
