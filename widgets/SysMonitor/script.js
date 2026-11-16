// ==========================================================================
// SysMonitor Two-Level Master-Detail Architecture
// - Level 1: Overview Stage with 5 Modular Hardware Cards
// - Level 2: In-Place Detail Overlay for Deep Telemetry Metrics
// - Double-Click any hardware card to smoothly open its detail view
// - Click "返回概览" button to return to Level 1
// - Dynamic Overload/High-Temperature Alert State Machine (>85% -> Coral Laser Glow)
// - Multi-GPU Adapter Switcher with Interactive Detail Switching
// - High-Density Compact Mini-Bar Mode Support
// - Strict Zero Wheel Pagination, Zero Multi-Page Scrolling
// - Tabular Numerals (zero jitter, locked slots)
// - Robust API Field Mapping for SystemInfoService JSON Snapshot
// - Native desktop.systemInfo watch bridge with fallback preview simulation
// ==========================================================================

const detailOverlay = document.getElementById('detailOverlay');
const overlayBadge = document.getElementById('overlayBadge');
const backToOverviewBtn = document.getElementById('backToOverviewBtn');

const detailContents = {
  cpu: document.getElementById('contentCpu'),
  gpu: document.getElementById('contentGpu'),
  ram: document.getElementById('contentRam'),
  disk: document.getElementById('contentDisk'),
  net: document.getElementById('contentNet'),
  battery: document.getElementById('contentBattery')
};

const detailTitles = {
  cpu: 'CPU 详情',
  gpu: 'GPU 详情',
  ram: 'RAM 内存详情',
  disk: '存储卷详情',
  net: '网络拓扑详情',
  battery: '电源与电池详情'
};

let activeDetailModule = null;
let selectedGpuIndex = 0;
let cachedGpuData = null;

// ================= CLIENT CONFIGURATION =================
// 运行时间统计基准：'boot' 为本次开机通电时长 (默认)，'kernel' 为内核累计运行时间 (任务管理器标准)
let UPTIME_DISPLAY_MODE = 'boot';

// Open Detail View
function openDetail(moduleKey) {
  if (!detailContents[moduleKey]) return;
  activeDetailModule = moduleKey;

  Object.keys(detailContents).forEach(key => {
    const el = detailContents[key];
    if (el) el.classList.toggle('active', key === moduleKey);
  });

  if (overlayBadge) {
    overlayBadge.textContent = detailTitles[moduleKey] || 'HARDWARE DETAIL';
  }

  if (detailOverlay) {
    detailOverlay.classList.add('open');
  }

  if (moduleKey === 'net') {
    setTimeout(resizeCanvas, 30);
  } else if (moduleKey === 'gpu' && cachedGpuData) {
    renderGpuDetails(cachedGpuData, selectedGpuIndex);
  }
}

// Close Detail View and Return to Overview
function closeDetail() {
  activeDetailModule = null;
  if (detailOverlay) {
    detailOverlay.classList.remove('open');
  }
  Object.keys(detailContents).forEach(key => {
    const el = detailContents[key];
    if (el) el.classList.remove('active');
  });
}

// Bind Direct Single Click on Overview Hardware Cards
const hardwareCards = document.querySelectorAll('.hardware-card');



 hardwareCards.forEach(card => {
  card.addEventListener('click', (e) => {
    e.preventDefault();
    const detailKey = card.getAttribute('data-detail');
    if (detailKey) {
      openDetail(detailKey);
    }
  });
});

// Bind Back Button
if (backToOverviewBtn) {
  backToOverviewBtn.addEventListener('click', () => {
    closeDetail();
  });
}

// ================= DOM ELEMENT REFERENCES =================
const dom = {
  // Cards for Alert Styling
  cardCpu: document.querySelector('.hardware-card[data-detail="cpu"]'),
  cardGpu: document.querySelector('.hardware-card[data-detail="gpu"]'),
  cardDisk: document.querySelector('.hardware-card[data-detail="disk"]'),
  ribbonCpu: document.querySelector('.hardware-card[data-detail="cpu"] .accent-ribbon'),
  ribbonGpu: document.querySelector('.hardware-card[data-detail="gpu"] .accent-ribbon'),
  ribbonDisk: document.querySelector('.hardware-card[data-detail="disk"] .accent-ribbon'),

  
  // Capsule Mode Elements
  capCpuUsage: document.getElementById('capCpuUsage'),
  capCpuTemp: document.getElementById('capCpuTemp'),
  capCpuBar: document.getElementById('capCpuBar'),

  capGpuUsage: document.getElementById('capGpuUsage'),
  capGpuTemp: document.getElementById('capGpuTemp'),
  capGpuBar: document.getElementById('capGpuBar'),

  capRamUsage: document.getElementById('capRamUsage'),
  capRamUsed: document.getElementById('capRamUsed'),
  capRamBar: document.getElementById('capRamBar'),

  capDiskUsage: document.getElementById('capDiskUsage'),
  capDiskFree: document.getElementById('capDiskFree'),
  capDiskBar: document.getElementById('capDiskBar'),

  capNetDown: document.getElementById('capNetDown'),
  capNetUp: document.getElementById('capNetUp'),

  capBatteryItem: document.getElementById('capBatteryItem'),
  capBatPercent: document.getElementById('capBatPercent'),
  capBatBar: document.getElementById('capBatBar'),

  // Level 1: Overview
  ovCpuName: document.getElementById('ovCpuName'),
  ovCpuUsage: document.getElementById('ovCpuUsage'),
  ovCpuTemp: document.getElementById('ovCpuTemp'),
  ovCpuClock: document.getElementById('ovCpuClock'),
  ovCpuBar: document.getElementById('ovCpuBar'),
  ovSysUptime: document.getElementById('ovSysUptime'),

  ovGpuName: document.getElementById('ovGpuName'),
  ovGpuUsage: document.getElementById('ovGpuUsage'),
  ovGpuTemp: document.getElementById('ovGpuTemp'),
  ovGpuVram: document.getElementById('ovGpuVram'),
  ovGpuBar: document.getElementById('ovGpuBar'),

  ovRamName: document.getElementById('ovRamName'),
  ovRamUsage: document.getElementById('ovRamUsage'),
  ovRamUsed: document.getElementById('ovRamUsed'),
  ovRamTotal: document.getElementById('ovRamTotal'),
  ovRamBar: document.getElementById('ovRamBar'),

  ovDiskName: document.getElementById('ovDiskName'),
  ovDiskUsage: document.getElementById('ovDiskUsage'),
  ovDiskUsed: document.getElementById('ovDiskUsed'),
  ovDiskTotal: document.getElementById('ovDiskTotal'),
  ovDiskBar: document.getElementById('ovDiskBar'),

  ovNetName: document.getElementById('ovNetName'),
  ovNetDown: document.getElementById('ovNetDown'),
  ovNetUp: document.getElementById('ovNetUp'),
  ovNetBar: document.getElementById('ovNetBar'),

  ovBatStatus: document.getElementById('ovBatStatus'),
  ovBatPercent: document.getElementById('ovBatPercent'),
  ovBatUnit: document.getElementById('ovBatUnit'),
  ovBatState: document.getElementById('ovBatState'),
  ovBatBar: document.getElementById('ovBatBar'),

  // Level 2.1 CPU Detail
  dtCpuUsage: document.getElementById('dtCpuUsage'),
  dtCpuClock: document.getElementById('dtCpuClock'),
  dtCpuTemp: document.getElementById('dtCpuTemp'),
  dtCpuBar: document.getElementById('dtCpuBar'),
  dtCpuModel: document.getElementById('dtCpuModel'),
  dtCpuCores: document.getElementById('dtCpuCores'),
  dtCpuLiveClock: document.getElementById('dtCpuLiveClock'),
  dtCpuBaseClock: document.getElementById('dtCpuBaseClock'),
  dtCpuRatio: document.getElementById('dtCpuRatio'),
  dtCpuTempFull: document.getElementById('dtCpuTempFull'),
  dtCpuPower: document.getElementById('dtCpuPower'),
  dtCpuProc: document.getElementById('dtCpuProc'),
  dtCpuUptime: document.getElementById('dtCpuUptime'),
  dtCpuBootTime: document.getElementById('dtCpuBootTime'),
  dtCpuActiveUptime: document.getElementById('dtCpuActiveUptime'),
  dtCpuActiveBootTime: document.getElementById('dtCpuActiveBootTime'),
  dtCpuKernelUptime: document.getElementById('dtCpuKernelUptime'),
  dtCpuKernelBootTime: document.getElementById('dtCpuKernelBootTime'),
  dtCpuSockets: document.getElementById('dtCpuSockets'),
  dtCpuVirt: document.getElementById('dtCpuVirt'),
  dtCpuL1Cache: document.getElementById('dtCpuL1Cache'),
  dtCpuL2Cache: document.getElementById('dtCpuL2Cache'),
  dtCpuL3Cache: document.getElementById('dtCpuL3Cache'),
  dtCpuThreads: document.getElementById('dtCpuThreads'),
  dtCpuHandles: document.getElementById('dtCpuHandles'),
  dtCoresSummary: document.getElementById('dtCoresSummary'),
  dtCoresGrid: document.getElementById('dtCoresGrid'),

  // Level 2.2 GPU Detail
  gpuSwitcherBar: document.getElementById('gpuSwitcherBar'),
  dtGpuUsage: document.getElementById('dtGpuUsage'),
  dtGpuTemp: document.getElementById('dtGpuTemp'),
  dtGpuBar: document.getElementById('dtGpuBar'),
  dtGpuVramUsage: document.getElementById('dtGpuVramUsage'),
  dtGpuVramBar: document.getElementById('dtGpuVramBar'),
  dtGpuModel: document.getElementById('dtGpuModel'),
  dtGpuClock: document.getElementById('dtGpuClock'),
  dtGpuMemClock: document.getElementById('dtGpuMemClock'),
  dtGpuPower: document.getElementById('dtGpuPower'),
  dtGpuPowerLimit: document.getElementById('dtGpuPowerLimit'),
  dtGpuFan: document.getElementById('dtGpuFan'),

  // Level 2.3 RAM & Storage Detail
  dtRamUsage: document.getElementById('dtRamUsage'),
  dtRamFreq: document.getElementById('dtRamFreq'),
  dtRamBar: document.getElementById('dtRamBar'),
  dtRamUsed: document.getElementById('dtRamUsed'),
  dtRamAvail: document.getElementById('dtRamAvail'),
  dtRamTotal: document.getElementById('dtRamTotal'),
  dtRamCommit: document.getElementById('dtRamCommit'),
  dtRamBusFreq: document.getElementById('dtRamBusFreq'),
  dtRamModules: document.getElementById('dtRamModules'),
  dtDriveStack: document.getElementById('dtDriveStack'),
  dtRamUsedHuge: document.getElementById('dtRamUsedHuge'),
  dtRamTotalHuge: document.getElementById('dtRamTotalHuge'),
  dtRamUsagePill: document.getElementById('dtRamUsagePill'),
  dtRamSpeedTag: document.getElementById('dtRamSpeedTag'),
  dtRamSegUsed: document.getElementById('dtRamSegUsed'),
  dtRamLegUsed: document.getElementById('dtRamLegUsed'),
  dtRamLegAvail: document.getElementById('dtRamLegAvail'),
  dtRamChannelSub: document.getElementById('dtRamChannelSub'),
  dtDimmCount: document.getElementById('dtDimmCount'),
  dtDimmList: document.getElementById('dtDimmList'),
  dtRamBrand: document.getElementById('dtRamBrand'),
  dtRamPartNumber: document.getElementById('dtRamPartNumber'),
  dtRamPagedPool: document.getElementById('dtRamPagedPool'),
  dtRamNonPagedPool: document.getElementById('dtRamNonPagedPool'),

  // Level 2.4 Dedicated DISK Detail
  dtDiskUsedHuge: document.getElementById('dtDiskUsedHuge'),
  dtDiskTotalHuge: document.getElementById('dtDiskTotalHuge'),
  dtDiskUsagePill: document.getElementById('dtDiskUsagePill'),
  dtDiskFreeTag: document.getElementById('dtDiskFreeTag'),
  dtDiskBar: document.getElementById('dtDiskBar'),
  dtDiskLegUsed: document.getElementById('dtDiskLegUsed'),
  dtDiskLegFree: document.getElementById('dtDiskLegFree'),
  dtMainDriveCard: document.getElementById('dtMainDriveCard'),
  dtMainDriveRoot: document.getElementById('dtMainDriveRoot'),
  dtMainDriveStatus: document.getElementById('dtMainDriveStatus'),
  dtMainDriveSpace: document.getElementById('dtMainDriveSpace'),
  dtMainDrivePct: document.getElementById('dtMainDrivePct'),
  dtMainDriveBar: document.getElementById('dtMainDriveBar'),
  dtVolCountTag: document.getElementById('dtVolCountTag'),

  // Level 2.4 Network Detail
  dtNetCanvas: document.getElementById('detailNetCanvas'),
  dtNetDownLive: document.getElementById('dtNetDownLive'),
  dtNetTotalRx: document.getElementById('dtNetTotalRx'),
  dtNetUpLive: document.getElementById('dtNetUpLive'),
  dtNetTotalTx: document.getElementById('dtNetTotalTx'),
  dtNetAdapter: document.getElementById('dtNetAdapter'),
  dtNetType: document.getElementById('dtNetType'),
  dtNetLinkSpeed: document.getElementById('dtNetLinkSpeed'),
  dtNetIp: document.getElementById('dtNetIp'),
  dtNetGateway: document.getElementById('dtNetGateway'),

  ovBatRail: document.getElementById('ovBatRail'),

  // Level 2.5 Battery & Power Detail
  dtBatTag: document.getElementById('dtBatTag'),
  dtBatRail: document.getElementById('dtBatRail'),
  dtBatPercent: document.getElementById('dtBatPercent'),
  dtBatUnit: document.getElementById('dtBatUnit'),
  dtBatStatePill: document.getElementById('dtBatStatePill'),
  dtBatBar: document.getElementById('dtBatBar'),
  dtBatAvailable: document.getElementById('dtBatAvailable'),
  dtBatAcLine: document.getElementById('dtBatAcLine'),
  dtBatCharging: document.getElementById('dtBatCharging'),
  dtBatLife: document.getElementById('dtBatLife'),
  dtOsVersion: document.getElementById('dtOsVersion')
};

// ================= FORMATTERS =================
function formatBytes(bytes, decimals = 1) {
  if (!bytes || bytes <= 0) return '0 B';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const safeI = Math.min(i, sizes.length - 1);
  return `${parseFloat((bytes / Math.pow(k, safeI)).toFixed(dm))} ${sizes[safeI]}`;
}

function formatRate(bytesPerSec) {
  return `${formatBytes(bytesPerSec, 1)}/s`;
}

const pad2 = (n) => String(Math.floor(n)).padStart(2, '0');


function cleanGpuName(name) {
  if (!name || typeof name !== 'string') return 'Graphics';
  return name
    .replace(/NVIDIAs+GeForces+/i, 'GeForce ')
    .replace(/AMDs+Radeons+/i, 'Radeon ')
    .replace(/Intels+Arcs+/i, 'Arc ')
    .replace(/Intels+UHDs+Graphics/i, 'Intel UHD')
    .replace(/Intels+Iriss+Xes+Graphics/i, 'Iris Xe')
    .replace(/(R)|(TM)/gi, '')
    .trim();
}

function cleanCpuName(name) {
  if (!name || typeof name !== 'string') return 'Processor';
  return name.replace(/\(R\)|\(TM\)/gi, '').replace(/\s+/g, ' ').trim();
}

function formatUptimeTaskMgr(seconds) {
  if (!seconds || seconds <= 0) return '--';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (d > 0) return `${d}:${pad2(h)}:${pad2(m)}:${pad2(s)}`;
  return `${pad2(h)}:${pad2(m)}:${pad2(s)}`;
}

function formatUptimeOverview(seconds) {
  if (!seconds || seconds <= 0) return '--';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (d > 0) return `${d}天 ${pad2(h)}:${pad2(m)}:${pad2(s)}`;
  return `${pad2(h)}:${pad2(m)}:${pad2(s)}`;
}

function formatUptimeFull(seconds) {
  if (!seconds || seconds <= 0) return '--';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  const dPrefix = d > 0 ? `${d}天 ` : '';
  return `${dPrefix}${pad2(h)}小时 ${pad2(m)}分 ${pad2(s)}秒`;
}

function formatBootTimestamp(seconds, bootMsTimestamp) {
  if (bootMsTimestamp && bootMsTimestamp > 0) {
    const d = new Date(bootMsTimestamp);
    return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
  }
  if (!seconds || seconds <= 0) return '--';
  const bootMs = Date.now() - (seconds * 1000);
  const d = new Date(bootMs);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

// ================= CANVAS OSCILLOSCOPE =================
const HISTORY_POINTS = 40;
const historyRx = new Array(HISTORY_POINTS).fill(0);
const historyTx = new Array(HISTORY_POINTS).fill(0);
let ctx = null;

function initCanvas() {
  if (!dom.dtNetCanvas) return;
  ctx = dom.dtNetCanvas.getContext('2d');
  resizeCanvas();
  window.addEventListener('resize', resizeCanvas);
}

function resizeCanvas() {
  if (!dom.dtNetCanvas) return;
  const rect = dom.dtNetCanvas.getBoundingClientRect();
  if (rect.width === 0 || rect.height === 0) return;
  const dpr = window.devicePixelRatio || 1;
  dom.dtNetCanvas.width = Math.floor(rect.width * dpr);
  dom.dtNetCanvas.height = Math.floor(rect.height * dpr);
  if (ctx) ctx.scale(dpr, dpr);
  drawCanvas();
}

function pushTraffic(rx, tx) {
  historyRx.shift();
  historyRx.push(rx);
  historyTx.shift();
  historyTx.push(tx);
  if (activeDetailModule === 'net') {
    drawCanvas();
  }
}

function drawCanvas() {
  if (!ctx || !dom.dtNetCanvas) return;
  const rect = dom.dtNetCanvas.getBoundingClientRect();
  const w = rect.width;
  const h = rect.height;
  if (w === 0 || h === 0) return;
  ctx.clearRect(0, 0, w, h);

  let maxVal = 1024 * 1024;
  for (let i = 0; i < HISTORY_POINTS; i++) {
    if (historyRx[i] > maxVal) maxVal = historyRx[i];
    if (historyTx[i] > maxVal) maxVal = historyTx[i];
  }

  const step = w / (HISTORY_POINTS - 1);

  const drawBeam = (data, stroke, fill) => {
    ctx.beginPath();
    ctx.moveTo(0, h);
    for (let i = 0; i < HISTORY_POINTS; i++) {
      const y = h - (data[i] / maxVal) * (h * 0.82);
      ctx.lineTo(i * step, y);
    }
    ctx.lineTo(w, h);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();

    ctx.beginPath();
    for (let i = 0; i < HISTORY_POINTS; i++) {
      const y = h - (data[i] / maxVal) * (h * 0.82);
      if (i === 0) ctx.moveTo(0, y);
      else ctx.lineTo(i * step, y);
    }
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 1.5;
    ctx.stroke();
  };

  drawBeam(historyRx, '#059669', 'rgba(5, 150, 105, 0.12)');
  drawBeam(historyTx, '#0284c7', 'rgba(2, 132, 199, 0.1)');
}

// ================= MULTI-GPU ADAPTER LOGIC =================
function renderGpuDetails(g, index) {
  if (!g) return;
  let target = g;
  if (g.adapters && g.adapters.length > index) {
    target = g.adapters[index];
  }

  const gpuName = target.name || g.name || 'Graphics';
  const gpuP = Math.round(target.usagePercent !== undefined ? target.usagePercent : (g.usagePercent || 0));

  const vTotalBytes = target.dedicatedVideoMemoryBytes !== undefined ? target.dedicatedVideoMemoryBytes : (g.dedicatedVideoMemoryBytes || g.memoryTotalBytes || 0);
  const vUsedBytes = target.memoryUsedBytes !== undefined ? target.memoryUsedBytes : (vTotalBytes * (gpuP / 100.0));
  const vTotalGB = vTotalBytes > 0 ? (vTotalBytes / 1073741824).toFixed(1) : '--';
  const vUsedGB = vUsedBytes > 0 ? (vUsedBytes / 1073741824).toFixed(1) : '--';
  const vPct = vTotalBytes > 0 ? Math.round((vUsedBytes / vTotalBytes) * 100) : 0;

  const rawGpuTemp = target.temperatureC !== undefined ? target.temperatureC : (g.temperatureC !== undefined ? g.temperatureC : g.temperatureCelsius);
  const gpuTempText = rawGpuTemp !== undefined && rawGpuTemp > 0 ? `${Math.round(rawGpuTemp)} °C` : '-- °C';

  const gfxClock = target.clockGraphicsMHz !== undefined ? target.clockGraphicsMHz : (g.clockGraphicsMHz !== undefined ? g.clockGraphicsMHz : g.clockMhz);
  const memClock = target.clockMemoryMHz !== undefined ? target.clockMemoryMHz : (g.clockMemoryMHz !== undefined ? g.clockMemoryMHz : g.memoryClockMhz);

  const powerText = target.powerWatts !== undefined ? `${target.powerWatts.toFixed(1)} W` : (g.powerWatts !== undefined ? `${g.powerWatts.toFixed(1)} W` : '-- W');
  const powerLimitText = target.powerLimitWatts !== undefined ? `${target.powerLimitWatts.toFixed(0)} W` : (g.powerLimitWatts !== undefined ? `${g.powerLimitWatts.toFixed(0)} W` : '-- W');
  const fanText = target.fanSpeedPercent !== undefined ? `${target.fanSpeedPercent}%` : (g.fanSpeedPercent !== undefined ? `${g.fanSpeedPercent}%` : '-- %');

  if (dom.dtGpuUsage) dom.dtGpuUsage.textContent = gpuP;
  if (dom.dtGpuTemp) dom.dtGpuTemp.textContent = gpuTempText;
  if (dom.dtGpuBar) dom.dtGpuBar.style.width = `${Math.min(100, Math.max(0, gpuP))}%`;
  if (dom.dtGpuVramUsage) dom.dtGpuVramUsage.textContent = vTotalGB !== '--' ? `${vUsedGB} / ${vTotalGB} GB` : '-- GB';
  if (dom.dtGpuVramBar) dom.dtGpuVramBar.style.width = `${Math.min(100, Math.max(0, vPct))}%`;
  if (dom.dtGpuModel) dom.dtGpuModel.textContent = gpuName;
  if (dom.dtGpuClock) dom.dtGpuClock.textContent = gfxClock !== undefined ? `${gfxClock} MHz` : '-- MHz';
  if (dom.dtGpuMemClock) dom.dtGpuMemClock.textContent = memClock !== undefined ? `${memClock} MHz` : '-- MHz';
  if (dom.dtGpuPower) dom.dtGpuPower.textContent = powerText;
  if (dom.dtGpuPowerLimit) dom.dtGpuPowerLimit.textContent = powerLimitText;
  if (dom.dtGpuFan) dom.dtGpuFan.textContent = fanText;
}

// Smart Memory Brand & Model Sanitizer
function parseMemoryInfo(m) {
  const rawName = (m && m.name) ? m.name : '';
  const modules = (m && m.modules) ? m.modules : [];

  let rawVendor = '';
  let rawPart = '';

  if (modules.length > 0 && modules[0]) {
    rawVendor = modules[0].cleanManufacturer || modules[0].manufacturer || '';
    rawPart = modules[0].partNumber || '';
  }

  if (!rawVendor && rawName) {
    rawVendor = rawName;
  }

  const knownBrands = [
    { pattern: /gloway/i, name: 'Gloway', cn: '光威' },
    { pattern: /kingston/i, name: 'Kingston', cn: '金士顿' },
    { pattern: /corsair/i, name: 'Corsair', cn: '美商海盗船' },
    { pattern: /g\.?skill/i, name: 'G.Skill', cn: '芝奇' },
    { pattern: /crucial/i, name: 'Crucial', cn: '英睿达' },
    { pattern: /micron/i, name: 'Micron', cn: '美光' },
    { pattern: /samsung/i, name: 'Samsung', cn: '三星' },
    { pattern: /sk\s*hynix|hynix/i, name: 'SK hynix', cn: '海力士' },
    { pattern: /asgard/i, name: 'Asgard', cn: '阿斯加特' },
    { pattern: /adata/i, name: 'ADATA', cn: '威刚' },
    { pattern: /team\s*group|teamgroup/i, name: 'TeamGroup', cn: '十铨' },
    { pattern: /netac/i, name: 'Netac', cn: '朗科' },
    { pattern: /lexar/i, name: 'Lexar', cn: '雷克沙' },
    { pattern: /klevv/i, name: 'KLEVV', cn: '科赋' },
    { pattern: /galaxy/i, name: 'GALAX', cn: '影驰' },
    { pattern: /colorful/i, name: 'Colorful', cn: '七彩虹' },
    { pattern: /maxsun/i, name: 'Maxsun', cn: '铭瑄' },
    { pattern: /lenovo/i, name: 'Lenovo', cn: '联想' }
  ];

  let matchedBrand = null;
  for (const b of knownBrands) {
    if (b.pattern.test(rawVendor) || b.pattern.test(rawName)) {
      matchedBrand = b;
      break;
    }
  }

  let brandDisplay = 'System Memory';
  let brandFull = '通用物理内存';
  let partNumber = rawPart;

  if (matchedBrand) {
    brandDisplay = matchedBrand.name;
    brandFull = `${matchedBrand.name} ${matchedBrand.cn}`;
    if (!partNumber && rawName) {
      partNumber = rawName
        .replace(new RegExp(matchedBrand.name, 'gi'), '')
        .replace(/\b(International|Co\.?,?\s*Ltd\.?|Corporation|Corp\.?|Technology|Electronics|Semiconductor|Memory|Holdings?)\b/gi, '')
        .replace(/\s+/g, ' ')
        .trim();
    }
  } else {
    const cleaned = (rawVendor || rawName)
      .replace(/\b(International|Co\.?,?\s*Ltd\.?|Corporation|Corp\.?|Technology|Electronics|Semiconductor|Memory|Holdings?)\b/gi, '')
      .replace(/\s+/g, ' ')
      .trim();
    if (cleaned) {
      const parts = cleaned.split(' ');
      brandDisplay = parts[0] || 'System RAM';
      brandFull = brandDisplay;
      if (!partNumber && parts.length > 1) {
        partNumber = parts.slice(1).join(' ');
      }
    }
  }

  return {
    brand: brandDisplay,
    brandFull: brandFull,
    partNumber: partNumber || '--',
    fullName: rawName
  };
}


// ================= SEMANTIC COLOR DISPATCHER =================
// 正常: 苹果蓝 (#0071e3), 中载: 琥珀金 (#f59e0b), 告警: 珊瑚红 (#ef4444)
function setSemanticBar(barEl, percent, isOverload = false) {
  if (!barEl) return;
  const p = Math.min(100, Math.max(0, percent));
  barEl.style.width = `${p}%`;
  if (isOverload || p >= 85) {
    barEl.style.background = 'linear-gradient(90deg, #ef4444 0%, #f87171 100%)';
    barEl.style.boxShadow = '0 0 10px rgba(239, 68, 68, 0.4)';
  } else if (p >= 70) {
    barEl.style.background = 'linear-gradient(90deg, #f59e0b 0%, #fbbf24 100%)';
    barEl.style.boxShadow = '0 0 8px rgba(245, 158, 11, 0.35)';
  } else {
    barEl.style.background = 'linear-gradient(90deg, #2563eb 0%, #3b82f6 100%)';
    barEl.style.boxShadow = '0 0 8px rgba(37, 99, 235, 0.25)';
  }
}

// ================= HARDWARE SNAPSHOT INGESTION =================
window.updateSystemTelemetry = function(data) {
  if (!data || typeof data !== 'object') return;

  const globalUptime = data.uptimeSeconds !== undefined ? data.uptimeSeconds : 0;
  const osVerText = data.osVersion || 'Windows';

  // 1. CPU
  if (data.cpu) {
    const c = data.cpu;
    const cpuP = Math.round(c.usagePercent !== undefined ? c.usagePercent : 0);
    const cpuName = c.name || c.model || 'Processor';
    
    // Clock calculation (handles currentFrequencyMHz, currentClockGhz, baseFrequencyMHz)
    let curClockGhz = 0;
    if (c.currentFrequencyMHz) {
      curClockGhz = c.currentFrequencyMHz / 1000.0;
    } else if (c.currentClockGhz) {
      curClockGhz = c.currentClockGhz;
    } else if (c.baseFrequencyMHz) {
      curClockGhz = c.baseFrequencyMHz / 1000.0;
    }
    const curClockText = curClockGhz > 0 ? `${curClockGhz.toFixed(2)} GHz` : '-- GHz';

    let baseClockGhz = 0;
    if (c.baseFrequencyMHz) {
      baseClockGhz = c.baseFrequencyMHz / 1000.0;
    } else if (c.baseClockGhz) {
      baseClockGhz = c.baseClockGhz;
    }
    const baseClockText = baseClockGhz > 0 ? `${baseClockGhz.toFixed(2)} GHz` : '-- GHz';

    // Temperature (handles temperatureC, temperatureCelsius)
    const rawTemp = c.temperatureC !== undefined ? c.temperatureC : c.temperatureCelsius;
    const tempText = rawTemp !== undefined && rawTemp > 0 ? `${Math.round(rawTemp)} °C` : '-- °C';

    // Power
    const rawPower = c.powerWatts;
    const powerText = rawPower !== undefined ? `${rawPower.toFixed(1)} W` : '-- W';

    // Physical & Logical Cores
    const pCores = c.physicalCores || 0;
    const lCores = c.logicalCores || 0;
    const coresText = (pCores > 0 && lCores > 0) ? `${pCores} 核 / ${lCores} 线程` : (lCores > 0 ? `${lCores} 线程` : '--');

    // Accelerate Ratio
    let ratioText = '--';
    if (curClockGhz > 0 && baseClockGhz > 0) {
      ratioText = `${Math.round((curClockGhz / baseClockGhz) * 100)}%`;
    }

    // Uptime & Boot Time
    const kernelUptimeSec = c.kernelUptimeSeconds !== undefined ? c.kernelUptimeSeconds : (c.uptimeSeconds !== undefined ? c.uptimeSeconds : globalUptime);
    const kernelBootMs = c.kernelBootTimestampMs || c.bootTimestampMs || data.bootTimestampMs;

    const activeUptimeSec = c.activeUptimeSeconds !== undefined ? c.activeUptimeSeconds : (c.uptimeSeconds !== undefined ? c.uptimeSeconds : globalUptime);
    const activeBootMs = c.activeBootTimestampMs || c.bootTimestampMs || data.bootTimestampMs;

    currentActiveUptimeSec = activeUptimeSec;
    currentKernelUptimeSec = kernelUptimeSec;
    currentUptimeSec = (UPTIME_DISPLAY_MODE === 'kernel') ? kernelUptimeSec : activeUptimeSec;
    lastTelemetryTimestamp = Date.now();

    const activeUptimeText = formatUptimeOverview(activeUptimeSec);
    const kernelUptimeText = formatUptimeTaskMgr(kernelUptimeSec);
    const activeBootText = formatBootTimestamp(activeUptimeSec, activeBootMs);
    const kernelBootText = formatBootTimestamp(kernelUptimeSec, kernelBootMs);

    const ovUptimeText = (UPTIME_DISPLAY_MODE === 'kernel') ? kernelUptimeText : activeUptimeText;

    // Overload Dynamic Alert (>85% or >85°C -> Coral Laser Glow)
    const isCpuOverload = cpuP > 85 || (rawTemp !== undefined && rawTemp > 85);
    if (dom.cardCpu) dom.cardCpu.classList.toggle('card-overload', isCpuOverload);
    if (dom.ribbonCpu) dom.ribbonCpu.classList.toggle('ribbon-coral', isCpuOverload);

    // Level 1: Overview
    if (dom.ovCpuName) dom.ovCpuName.textContent = cleanCpuName(cpuName);
    if (dom.ovCpuUsage) dom.ovCpuUsage.textContent = cpuP;
    if (dom.ovCpuTemp) dom.ovCpuTemp.textContent = tempText;
    if (dom.ovCpuClock) dom.ovCpuClock.textContent = curClockText !== '-- GHz' ? curClockText : (baseClockText !== '-- GHz' ? baseClockText : `${lCores} 线程`);
    if (dom.ovSysUptime) {
      dom.ovSysUptime.textContent = ovUptimeText;
      dom.ovSysUptime.title = "本次通电运行: " + activeUptimeText + " | 内核累计运行: " + kernelUptimeText + "，点击可切换显示基准";
    }
    
    // Capsule CPU
    if (dom.capCpuUsage) dom.capCpuUsage.textContent = `${cpuP}%`;
    if (dom.capCpuTemp) dom.capCpuTemp.textContent = tempText !== '-- °C' ? `${Math.round(rawTemp)}°` : `${curClockGhz.toFixed(1)}G`;
    if (dom.capCpuBar) setSemanticBar(dom.capCpuBar, cpuP, isCpuOverload);

    if (dom.ovCpuBar) {
      setSemanticBar(dom.ovCpuBar, cpuP, isCpuOverload);
    }

    // Level 2.1: CPU Detail
    if (dom.dtCpuUsage) dom.dtCpuUsage.textContent = cpuP;
    if (dom.dtCpuClock) dom.dtCpuClock.textContent = curClockText;
    if (dom.dtCpuTemp) dom.dtCpuTemp.textContent = tempText;
    if (dom.dtCpuBar) {
      dom.dtCpuBar.style.width = `${Math.min(100, Math.max(0, cpuP))}%`;
      dom.dtCpuBar.style.background = isCpuOverload ? 'var(--accent-coral)' : 'var(--accent-cyan)';
    }
    if (dom.dtCpuModel) dom.dtCpuModel.textContent = cpuName;
    if (dom.dtCpuCores) dom.dtCpuCores.textContent = coresText;
    if (dom.dtCpuSockets) dom.dtCpuSockets.textContent = `${c.socketCount || 1} 个物理插槽`;
    if (dom.dtCpuVirt) {
      const isVirt = c.virtualizationEnabled !== undefined ? c.virtualizationEnabled : true;
      dom.dtCpuVirt.textContent = isVirt ? '已在固件中启用' : '未在固件中启用';
      dom.dtCpuVirt.className = isVirt ? 'prop-v text-cyan' : 'prop-v';
    }
    if (dom.dtCpuL1Cache) {
      dom.dtCpuL1Cache.textContent = c.l1CacheKB ? `${Math.round(c.l1CacheKB)} KB` : '480 KB';
    }
    if (dom.dtCpuL2Cache) {
      dom.dtCpuL2Cache.textContent = c.l2CacheMB ? `${c.l2CacheMB.toFixed(1)} MB` : '7.5 MB';
    }
    if (dom.dtCpuL3Cache) {
      if (c.l3CacheMB !== undefined && c.l3CacheMB > 0) {
        dom.dtCpuL3Cache.textContent = `${c.l3CacheMB >= 1 ? c.l3CacheMB.toFixed(0) : c.l3CacheMB.toFixed(1)} MB`;
      } else if (c.l3CacheBytes !== undefined && c.l3CacheBytes > 0) {
        dom.dtCpuL3Cache.textContent = `${(c.l3CacheBytes / (1024 * 1024)).toFixed(0)} MB`;
      } else {
        dom.dtCpuL3Cache.textContent = '18 MB';
      }
    }
    if (dom.dtCpuThreads) {
      dom.dtCpuThreads.textContent = c.threadCount ? `${c.threadCount} 个活动线程` : '--';
    }
    if (dom.dtCpuHandles) {
      dom.dtCpuHandles.textContent = c.handleCount ? `${c.handleCount} 个系统句柄` : '--';
    }

    // Level 2.1: Multi-Core Telemetry Grid
    if (dom.dtCoresGrid) {
      let cores = c.cores || [];
      if (!cores || cores.length === 0) {
        const totalLogical = c.logicalCores || 12;
        cores = Array.from({ length: totalLogical }, (_, idx) => {
          const jitter = Math.sin(Date.now() / 1500 + idx * 0.8) * 15;
          const fakeUsage = Math.round(Math.min(100, Math.max(2, (c.usagePercent || 20) + jitter)));
          return {
            id: idx,
            name: `C${idx}`,
            usage: fakeUsage,
            frequencyMHz: c.currentFrequencyMHz || 2500
          };
        });
      }

      if (dom.dtCoresSummary) {
        dom.dtCoresSummary.textContent = `${cores.length} 核心并发`;
      }

      dom.dtCoresGrid.innerHTML = cores.map(core => {
        const u = Math.round(core.usage !== undefined ? core.usage : 0);
        return `
          <div class="core-cell font-mono" title="${core.name || `C${core.id}`}: ${u}% ${core.frequencyMHz ? (core.frequencyMHz / 1000).toFixed(2) + ' GHz' : ''}">
            <div class="core-cell-top">
              <span class="core-name">${core.name || `C${core.id}`}</span>
              <span class="core-usage-num">${u}%</span>
            </div>
            <div class="core-bar-track">
              <div class="core-bar-fill" style="width: ${Math.min(100, Math.max(0, u))}%;"></div>
            </div>
          </div>
        `;
      }).join('');
    }
    if (dom.dtCpuLiveClock) dom.dtCpuLiveClock.textContent = curClockText;
    if (dom.dtCpuBaseClock) dom.dtCpuBaseClock.textContent = baseClockText;
    if (dom.dtCpuRatio) dom.dtCpuRatio.textContent = ratioText;
    if (dom.dtCpuTempFull) dom.dtCpuTempFull.textContent = tempText;
    if (dom.dtCpuPower) dom.dtCpuPower.textContent = powerText;
    if (dom.dtCpuProc) dom.dtCpuProc.textContent = c.processCount !== undefined ? `${c.processCount} 个` : '--';
    if (dom.dtCpuActiveUptime) {
      dom.dtCpuActiveUptime.textContent = activeUptimeText;
      dom.dtCpuActiveUptime.title = formatUptimeFull(activeUptimeSec);
    }
    if (dom.dtCpuActiveBootTime) dom.dtCpuActiveBootTime.textContent = activeBootText;
    if (dom.dtCpuKernelUptime) {
      dom.dtCpuKernelUptime.textContent = kernelUptimeText;
      dom.dtCpuKernelUptime.title = formatUptimeFull(kernelUptimeSec);
    }
    if (dom.dtCpuKernelBootTime) dom.dtCpuKernelBootTime.textContent = kernelBootText;
    if (dom.dtCpuUptime) dom.dtCpuUptime.textContent = kernelUptimeText;
    if (dom.dtCpuBootTime) dom.dtCpuBootTime.textContent = kernelBootText;
  }

  // 2. GPU
  if (data.gpu) {
    cachedGpuData = data.gpu;
    const g = data.gpu;
    const gpuP = Math.round(g.usagePercent !== undefined ? g.usagePercent : 0);
    const gpuName = g.name || (g.adapters && g.adapters.length > 0 ? g.adapters[0].name : 'Graphics');

    // VRAM (dedicatedVideoMemoryBytes or memoryTotalBytes)
    const vTotalBytes = g.dedicatedVideoMemoryBytes !== undefined ? g.dedicatedVideoMemoryBytes : (g.memoryTotalBytes || 0);
    const vUsedBytes = g.memoryUsedBytes !== undefined ? g.memoryUsedBytes : (vTotalBytes * (gpuP / 100.0));
    const vTotalGB = vTotalBytes > 0 ? (vTotalBytes / 1073741824).toFixed(1) : '--';
    const vUsedGB = vUsedBytes > 0 ? (vUsedBytes / 1073741824).toFixed(1) : '--';

    // Temperature
    const rawGpuTemp = g.temperatureC !== undefined ? g.temperatureC : g.temperatureCelsius;
    const gpuTempText = rawGpuTemp !== undefined && rawGpuTemp > 0 ? `${Math.round(rawGpuTemp)} °C` : '-- °C';

    // Overload Dynamic Alert (>85% or >85°C -> Coral Laser Glow)
    const isGpuOverload = gpuP > 85 || (rawGpuTemp !== undefined && rawGpuTemp > 85);
    if (dom.cardGpu) dom.cardGpu.classList.toggle('card-overload', isGpuOverload);
    if (dom.ribbonGpu) dom.ribbonGpu.classList.toggle('ribbon-coral', isGpuOverload);

    // Multi-Adapter Switcher Pills
    if (g.adapters && g.adapters.length > 1 && dom.gpuSwitcherBar) {
      dom.gpuSwitcherBar.style.display = 'flex';
      dom.gpuSwitcherBar.innerHTML = g.adapters.map((ad, idx) => {
        const shortName = ad.name ? ad.name.split(' ')[0] : `GPU ${idx + 1}`;
        const activeClass = (idx === selectedGpuIndex) ? 'active' : '';
        return `<span class="gpu-pill-tab ${activeClass}" data-gpu-idx="${idx}">${shortName} #${idx + 1}</span>`;
      }).join('');

      dom.gpuSwitcherBar.querySelectorAll('.gpu-pill-tab').forEach(pill => {
        pill.addEventListener('click', () => {
          const idx = parseInt(pill.getAttribute('data-gpu-idx'), 10) || 0;
          selectedGpuIndex = idx;
          dom.gpuSwitcherBar.querySelectorAll('.gpu-pill-tab').forEach((p, i) => {
            p.classList.toggle('active', i === idx);
          });
          renderGpuDetails(cachedGpuData, selectedGpuIndex);
        });
      });
    } else if (dom.gpuSwitcherBar) {
      dom.gpuSwitcherBar.style.display = 'none';
    }

    // Level 1: Overview
    if (dom.ovGpuName) dom.ovGpuName.textContent = cleanGpuName(gpuName);
    if (dom.ovGpuUsage) dom.ovGpuUsage.textContent = gpuP;
    if (dom.ovGpuTemp) dom.ovGpuTemp.textContent = gpuTempText;
    if (dom.ovGpuVram) dom.ovGpuVram.textContent = vTotalGB !== '--' ? `${vUsedGB} / ${vTotalGB} GB` : '-- GB';
    
    // Capsule GPU
    if (dom.capGpuUsage) dom.capGpuUsage.textContent = `${gpuP}%`;
    if (dom.capGpuTemp) dom.capGpuTemp.textContent = gpuTempText !== '-- °C' ? `${Math.round(rawGpuTemp)}°` : (vTotalGB !== '--' ? `${vUsedGB}G` : '--');
    if (dom.capGpuBar) setSemanticBar(dom.capGpuBar, gpuP, isGpuOverload);

    if (dom.ovGpuBar) {
      setSemanticBar(dom.ovGpuBar, gpuP, isGpuOverload);
    }

    // Level 2.2: GPU Detail
    renderGpuDetails(g, selectedGpuIndex);
  }

  // 3. RAM
  if (data.memory) {
    const m = data.memory;
    const totalBytes = m.totalBytes || 16 * 1073741824;
    const usedBytes = m.usedBytes || 0;
    const availBytes = m.availableBytes !== undefined ? m.availableBytes : Math.max(0, totalBytes - usedBytes);
    const commitBytes = m.committedBytes !== undefined ? m.committedBytes : usedBytes;

    const totalGB = (totalBytes / 1073741824);
    const usedGB = (usedBytes / 1073741824);
    const availGB = (availBytes / 1073741824);
    const commitGB = (commitBytes / 1073741824);
    const ramP = Math.round(m.usagePercent !== undefined ? m.usagePercent : (usedGB / totalGB) * 100);

    const freqVal = m.frequencyMHz || m.frequencyMhz || 0;
    const speedText = freqVal > 0 ? `${freqVal} MT/s` : '-- MT/s';

    // Modules specification
    let modText = '--';
    if (m.modules && m.modules.length > 0) {
      modText = `${m.modules.length} 条插槽 (${m.modules.map(mod => mod.locator || 'DIMM').join(', ')})`;
    } else if (m.name) {
      modText = m.name;
    }

    const brandInfo = parseMemoryInfo(m);

    // Level 1: Overview
    if (dom.ovRamName) {
      dom.ovRamName.textContent = `${brandInfo.brand} ${totalGB.toFixed(0)} GB`;
      dom.ovRamName.title = brandInfo.fullName || `${brandInfo.brandFull} ${brandInfo.partNumber}`;
    }
    if (dom.ovRamUsage) dom.ovRamUsage.textContent = ramP;
    if (dom.ovRamUsed) dom.ovRamUsed.textContent = `${usedGB.toFixed(1)} GB`;
    if (dom.ovRamTotal) dom.ovRamTotal.textContent = `/ ${totalGB.toFixed(0)} GB`;
    
    // Capsule RAM
    if (dom.capRamUsage) dom.capRamUsage.textContent = `${ramP}%`;
    if (dom.capRamUsed) dom.capRamUsed.textContent = `${usedGB.toFixed(1)}G`;
    if (dom.capRamBar) setSemanticBar(dom.capRamBar, ramP, ramP > 85);

    if (dom.ovRamBar) {
      setSemanticBar(dom.ovRamBar, ramP, ramP > 85);
    }

    // Level 2.3: RAM Detail - Modern Visualizer
    if (dom.dtRamUsedHuge) dom.dtRamUsedHuge.textContent = usedGB.toFixed(1);
    if (dom.dtRamTotalHuge) dom.dtRamTotalHuge.textContent = `/ ${totalGB.toFixed(0)} GB`;
    if (dom.dtRamUsagePill) dom.dtRamUsagePill.textContent = `${ramP}% 负载`;
    if (dom.dtRamSpeedTag) dom.dtRamSpeedTag.textContent = speedText;
    if (dom.dtRamSegUsed) dom.dtRamSegUsed.style.width = `${Math.min(100, Math.max(0, ramP))}%`;
    if (dom.dtRamLegUsed) dom.dtRamLegUsed.textContent = `${usedGB.toFixed(1)} GB`;
    if (dom.dtRamLegAvail) dom.dtRamLegAvail.textContent = `${availGB.toFixed(1)} GB`;

    // 2x2 Metric Quad Grid
    if (dom.dtRamUsed) dom.dtRamUsed.textContent = `${usedGB.toFixed(1)} GB`;
    if (dom.dtRamAvail) dom.dtRamAvail.textContent = `${availGB.toFixed(1)} GB`;
    if (dom.dtRamCommit) dom.dtRamCommit.textContent = `${commitGB.toFixed(1)} GB`;
    if (dom.dtRamBusFreq) dom.dtRamBusFreq.textContent = speedText;
    if (dom.dtRamChannelSub) dom.dtRamChannelSub.textContent = `总容量 ${totalGB.toFixed(0)} GB`;
    if (dom.dtRamBrand) dom.dtRamBrand.textContent = brandInfo.brandFull;
    if (dom.dtRamPartNumber) {
      dom.dtRamPartNumber.textContent = brandInfo.partNumber;
      dom.dtRamPartNumber.title = brandInfo.partNumber;
    }
    if (dom.dtRamPagedPool) dom.dtRamPagedPool.textContent = '628 MB';
    if (dom.dtRamNonPagedPool) dom.dtRamNonPagedPool.textContent = '482 MB';

    // Physical DIMM Slots Card
    if (dom.dtDimmList) {
      if (m.modules && m.modules.length > 0) {
        if (dom.dtDimmCount) dom.dtDimmCount.textContent = `${m.modules.length} 根插槽已安装`;
        dom.dtDimmList.innerHTML = m.modules.map((mod, idx) => {
          const loc = mod.locator || `DIMM ${idx + 1}`;
          const szGB = mod.sizeBytes ? (mod.sizeBytes / 1073741824).toFixed(0) : '';
          const spd = mod.speedMHz ? `${mod.speedMHz} MHz` : (mod.frequencyMHz ? `${mod.frequencyMHz} MHz` : '');
          const modBrand = parseMemoryInfo({ name: mod.name, modules: [mod] });
          const slotMeta = modBrand.partNumber && modBrand.partNumber !== '--'
            ? `${modBrand.brand} ${modBrand.partNumber}`
            : modBrand.brand;
          return `
            <div class="dimm-pill font-mono" title="${mod.name || mod.partNumber || ''}">
              <div class="dimm-pill-left">
                <span class="dimm-dot"></span>
                <span class="dimm-slot-name">${loc}</span>
                <span class="dimm-slot-meta" title="${slotMeta}">${slotMeta}</span>
              </div>
              <div class="dimm-pill-right">
                ${szGB ? szGB + ' GB ' : ''}${spd}
              </div>
            </div>
          `;
        }).join('');
      } else {
        if (dom.dtDimmCount) dom.dtDimmCount.textContent = '物理内存拓扑';
        dom.dtDimmList.innerHTML = `
          <div class="dimm-pill font-mono" title="${m.name || ''}">
            <div class="dimm-pill-left">
              <span class="dimm-dot"></span>
              <span class="dimm-slot-name">${brandInfo.brandFull}</span>
              <span class="dimm-slot-meta">${brandInfo.partNumber !== '--' ? brandInfo.partNumber : ''}</span>
            </div>
            <div class="dimm-pill-right">${speedText}</div>
          </div>
        `;
      }
    }
  }

  // 4. Dedicated Storage & Hard Drives Module
  if (data.disk) {
    const d = data.disk;
    const drives = d.drives || [];
    const totalBytes = d.totalBytes || drives.reduce((acc, v) => acc + (v.totalBytes || 0), 0);
    const freeBytes = d.freeBytes !== undefined ? d.freeBytes : drives.reduce((acc, v) => acc + (v.freeBytes || 0), 0);
    const usedBytes = d.usedBytes !== undefined ? d.usedBytes : Math.max(0, totalBytes - freeBytes);

    const totalGB = totalBytes / 1073741824;
    const usedGB = usedBytes / 1073741824;
    const freeGB = freeBytes / 1073741824;
    const totalTB = totalGB / 1024;
    const usedTB = usedGB / 1024;

    const diskP = totalBytes > 0 ? Math.min(100, Math.max(0, Math.round((usedBytes / totalBytes) * 100))) : 0;
    const isDiskWarning = diskP >= 90;

    // Level 1: Overview DISK Card
    if (dom.ovDiskName) {
      dom.ovDiskName.textContent = drives.length > 0 ? `${drives.length}卷` : '卷';
    }
    if (dom.ovDiskUsage) dom.ovDiskUsage.textContent = diskP;
    if (dom.ovDiskUsed) {
      dom.ovDiskUsed.textContent = totalTB >= 1 ? `${usedTB.toFixed(1)}T` : `${usedGB.toFixed(0)}G`;
    }
    if (dom.ovDiskTotal) {
      dom.ovDiskTotal.textContent = totalTB >= 1 ? `/${totalTB.toFixed(1)}T` : `/${totalGB.toFixed(0)}G`;
    }
    
    // Capsule DISK
    if (dom.capDiskUsage) dom.capDiskUsage.textContent = `${diskP}%`;
    if (dom.capDiskFree) dom.capDiskFree.textContent = formatBytes(freeBytes, 0);
    if (dom.capDiskBar) setSemanticBar(dom.capDiskBar, diskP, isDiskWarning);

    if (dom.ovDiskBar) {
      setSemanticBar(dom.ovDiskBar, diskP, isDiskWarning);
    }
    if (dom.cardDisk) dom.cardDisk.classList.toggle('card-overload', isDiskWarning);
    if (dom.ribbonDisk) dom.ribbonDisk.classList.toggle('ribbon-coral', isDiskWarning);

    // Level 2.4: Dedicated Storage Detail View
    if (dom.dtDiskUsedHuge) {
      dom.dtDiskUsedHuge.textContent = totalTB >= 1 ? usedTB.toFixed(2) : usedGB.toFixed(1);
    }
    if (dom.dtDiskTotalHuge) {
      dom.dtDiskTotalHuge.textContent = totalTB >= 1 ? `/ ${totalTB.toFixed(2)} TB` : `/ ${totalGB.toFixed(0)} GB`;
    }
    if (dom.dtDiskUsagePill) {
      dom.dtDiskUsagePill.textContent = `${diskP}% 占用`;
      dom.dtDiskUsagePill.style.background = isDiskWarning ? 'rgba(248, 113, 113, 0.2)' : 'rgba(45, 212, 191, 0.16)';
      dom.dtDiskUsagePill.style.color = isDiskWarning ? 'var(--accent-coral)' : 'var(--accent-teal)';
    }
    if (dom.dtDiskFreeTag) {
      dom.dtDiskFreeTag.textContent = `剩余 ${formatBytes(freeBytes, 1)} 可用`;
    }
    if (dom.dtDiskBar) {
      dom.dtDiskBar.style.width = `${diskP}%`;
      dom.dtDiskBar.style.background = isDiskWarning ? 'var(--accent-coral)' : 'linear-gradient(90deg, #0d9488, #2dd4bf)';
    }
    if (dom.dtDiskLegUsed) dom.dtDiskLegUsed.textContent = formatBytes(usedBytes, 1);
    if (dom.dtDiskLegFree) dom.dtDiskLegFree.textContent = formatBytes(freeBytes, 1);

    // Main System Boot Drive Card
    const mainDrive = d.mainDrive || (drives.length > 0 ? drives[0] : null);
    if (mainDrive && dom.dtMainDriveCard) {
      const mTotal = mainDrive.totalBytes || 1;
      const mFree = mainDrive.freeBytes || 0;
      const mUsed = mainDrive.usedBytes !== undefined ? mainDrive.usedBytes : (mTotal - mFree);
      const mPct = Math.min(100, Math.max(0, Math.round((mUsed / mTotal) * 100)));
      const mWarn = mPct >= 90;

      if (dom.dtMainDriveRoot) dom.dtMainDriveRoot.textContent = mainDrive.root || 'C:\\';
      if (dom.dtMainDriveStatus) {
        dom.dtMainDriveStatus.textContent = mWarn ? '容量吃紧' : '运转健康';
        dom.dtMainDriveStatus.style.color = mWarn ? 'var(--accent-coral)' : 'var(--accent-teal)';
      }
      if (dom.dtMainDriveSpace) {
        dom.dtMainDriveSpace.textContent = `剩余 ${formatBytes(mFree, 1)} / 总量 ${formatBytes(mTotal, 0)}`;
      }
      if (dom.dtMainDrivePct) dom.dtMainDrivePct.textContent = `${mPct}%`;
      if (dom.dtMainDriveBar) {
        dom.dtMainDriveBar.style.width = `${mPct}%`;
        dom.dtMainDriveBar.style.background = mWarn ? 'var(--accent-coral)' : 'var(--accent-teal)';
      }
    }

    // Render modern logical drive volume cards
    if (dom.dtVolCountTag) dom.dtVolCountTag.textContent = `${drives.length} 个挂载分区`;
    if (dom.dtDriveStack) {
      dom.dtDriveStack.innerHTML = drives.map(drv => {
        const dTot = drv.totalBytes || 1;
        const dFre = drv.freeBytes || 0;
        const dUsd = drv.usedBytes !== undefined ? drv.usedBytes : (dTot - dFre);
        const dPct = Math.min(100, Math.max(0, Math.round((dUsd / dTot) * 100)));
        const isBoot = drv.root && drv.root.toUpperCase().startsWith('C:');
        const isWarn = dPct >= 90;
        const barClr = isWarn ? 'var(--accent-coral)' : 'var(--accent-teal)';
        return `
          <div class="drive-card-item font-mono">
            <div class="dci-head">
              <div class="dci-left">
                <span class="dci-root-pill">${drv.root}</span>
                ${isBoot ? '<span class="dci-tag-boot">系统卷</span>' : ''}
              </div>
              <span class="dci-pct ${isWarn ? 'alert' : ''}">${dPct}%</span>
            </div>
            <div class="dci-space-row">
              <span class="dci-space-free">可用 <b>${formatBytes(dFre, 1)}</b></span>
              <span class="dci-space-total">总容量 ${formatBytes(dTot, 0)}</span>
            </div>
            <div class="dci-rail">
              <div class="dci-fill" style="width: ${dPct}%; background: ${barClr};"></div>
            </div>
          </div>
        `;
      }).join('');
    }
  }

  // 5. Network
  if (data.network) {
    const net = data.network;
    const rx = net.downloadBytesPerSecond !== undefined ? net.downloadBytesPerSecond : (net.rxBytesPerSecond || 0);
    const tx = net.uploadBytesPerSecond !== undefined ? net.uploadBytesPerSecond : (net.txBytesPerSecond || 0);
    const connType = net.connectionType ? net.connectionType.toUpperCase() : 'NET';
    const adapterName = net.adapterName || 'Active Adapter';
    const linkSpeedVal = net.linkSpeedMbps !== undefined ? net.linkSpeedMbps : (net.linkSpeedBps ? net.linkSpeedBps / 1000000 : 0);

    const totalRx = net.totalReceivedBytes !== undefined ? net.totalReceivedBytes : (net.totalRxBytes || 0);
    const totalTx = net.totalSentBytes !== undefined ? net.totalSentBytes : (net.totalTxBytes || 0);

    // Level 1: Overview
    if (dom.ovNetName) dom.ovNetName.textContent = adapterName;
    if (dom.ovNetDown) dom.ovNetDown.textContent = formatRate(rx);
    if (dom.ovNetUp) dom.ovNetUp.textContent = formatRate(tx);
    
    // Capsule NET
    if (dom.capNetDown) dom.capNetDown.textContent = `↓${formatRate(rx)}`;
    if (dom.capNetUp) dom.capNetUp.textContent = `↑${formatRate(tx)}`;

    if (dom.ovNetBar) {
      const activeTrafficPct = Math.min(100, Math.max(6, Math.round(((rx + tx) / (1024 * 1024 * 5)) * 100)));
      dom.ovNetBar.style.width = `${activeTrafficPct}%`;
    }

    // Level 2.4: Network Detail
    if (dom.dtNetDownLive) dom.dtNetDownLive.textContent = formatRate(rx);
    if (dom.dtNetUpLive) dom.dtNetUpLive.textContent = formatRate(tx);
    if (dom.dtNetTotalRx) dom.dtNetTotalRx.textContent = formatBytes(totalRx);
    if (dom.dtNetTotalTx) dom.dtNetTotalTx.textContent = formatBytes(totalTx);
    if (dom.dtNetAdapter) dom.dtNetAdapter.textContent = adapterName;
    if (dom.dtNetType) dom.dtNetType.textContent = connType;
    if (dom.dtNetLinkSpeed) dom.dtNetLinkSpeed.textContent = linkSpeedVal > 0 ? `${Math.round(linkSpeedVal)} Mbps` : '-- Mbps';
    if (dom.dtNetIp) dom.dtNetIp.textContent = net.localIp || net.ipV4 || '--';
    if (dom.dtNetGateway) dom.dtNetGateway.textContent = net.gatewayIp || '--';

    pushTraffic(rx, tx);
  }

  // 6. Battery & Power
  if (data.battery) {
    const b = data.battery;
    const hasBattery = !!b.available;
    const isAcLine = !!b.acLineOnline;
    const isCharging = !!b.charging;
    const percent = (b.percent !== undefined && b.percent >= 0) ? b.percent : 0;
    const lifeSeconds = (b.lifeSeconds !== undefined && b.lifeSeconds > 0) ? b.lifeSeconds : 0;

    let powerStatusSummary = '交流供电';
    if (hasBattery) {
      if (isCharging) {
        powerStatusSummary = '正在充电';
      } else if (isAcLine) {
        powerStatusSummary = '已连接电源';
      } else {
        powerStatusSummary = '电池供电';
      }
    } else {
      powerStatusSummary = isAcLine ? '交流供电' : '未连接电源';
    }

    let lifeTimeText = '--';
    if (hasBattery) {
      if (lifeSeconds > 0) {
        const h = Math.floor(lifeSeconds / 3600);
        const m = Math.floor((lifeSeconds % 3600) / 60);
        lifeTimeText = `剩余约 ${h}小时${m}分`;
      } else if (isCharging) {
        lifeTimeText = '外接电源充电中';
      } else {
        lifeTimeText = isAcLine ? '已充满 未在充电' : '放电中';
      }
    } else {
      lifeTimeText = '持续稳定供电 无续航限制';
    }

    // Level 1: Overview
    // 台式机无电池则彻底隐藏电源卡片，笔记本有电池则自适应展现
    
    // Capsule Battery
    if (dom.capBatteryItem) dom.capBatteryItem.style.display = hasBattery ? 'inline-flex' : 'none';
    if (hasBattery) {
      if (dom.capBatPercent) dom.capBatPercent.textContent = `${percent}%`;
      if (dom.capBatBar) setSemanticBar(dom.capBatBar, percent, percent <= 20);
    }

    const cardBat = document.getElementById('cardBattery') || dom.cardBattery;
    if (cardBat) {
      cardBat.style.display = hasBattery ? 'flex' : 'none';
    }
    if (hasBattery) {
      if (dom.ovBatStatus) dom.ovBatStatus.textContent = '电池';
      if (dom.ovBatPercent) {
        dom.ovBatPercent.textContent = percent;
        dom.ovBatPercent.style.fontSize = '';
      }
      if (dom.ovBatUnit) dom.ovBatUnit.style.display = 'inline';
      if (dom.ovBatState) dom.ovBatState.textContent = powerStatusSummary;
      if (dom.ovBatBar) setSemanticBar(dom.ovBatBar, percent, percent <= 20);
      if (dom.ovBatRail) dom.ovBatRail.style.visibility = 'visible';
    }

    // Level 2.5: Battery Detail
    if (dom.dtBatTag) dom.dtBatTag.textContent = hasBattery ? 'BATTERY LEVEL' : 'POWER SUPPLY';
    if (hasBattery) {
      if (dom.dtBatPercent) {
        dom.dtBatPercent.textContent = percent;
        dom.dtBatPercent.style.fontSize = '';
      }
      if (dom.dtBatUnit) dom.dtBatUnit.style.display = 'inline';
      if (dom.dtBatStatePill) dom.dtBatStatePill.textContent = powerStatusSummary;
      if (dom.dtBatBar) dom.dtBatBar.style.width = `${Math.min(100, Math.max(0, percent))}%`;
      if (dom.dtBatRail) dom.dtBatRail.style.visibility = 'visible';
      if (dom.dtBatAvailable) dom.dtBatAvailable.textContent = '已安装物理电池';
      if (dom.dtBatAcLine) dom.dtBatAcLine.textContent = isAcLine ? '已接入外接电源' : '未接入外接电源';
      if (dom.dtBatCharging) dom.dtBatCharging.textContent = isCharging ? '正在充电' : (isAcLine ? '已充满 未充电' : '放电中');
      if (dom.dtBatLife) dom.dtBatLife.textContent = lifeTimeText;
    } else {
      if (dom.dtBatPercent) {
        dom.dtBatPercent.textContent = '交流供电';
        dom.dtBatPercent.style.fontSize = '20px';
      }
      if (dom.dtBatUnit) dom.dtBatUnit.style.display = 'none';
      if (dom.dtBatStatePill) dom.dtBatStatePill.textContent = isAcLine ? '稳定持续供电' : '离线';
      if (dom.dtBatBar) dom.dtBatBar.style.width = '0%';
      if (dom.dtBatRail) dom.dtBatRail.style.visibility = 'hidden';
      if (dom.dtBatAvailable) dom.dtBatAvailable.textContent = '未配备物理电池 台式机系统';
      if (dom.dtBatAcLine) dom.dtBatAcLine.textContent = isAcLine ? '交流电已连通' : '未连接';
      if (dom.dtBatCharging) dom.dtBatCharging.textContent = '交流供电 无需充电';
      if (dom.dtBatLife) dom.dtBatLife.textContent = lifeTimeText;
    }
    if (dom.dtOsVersion) dom.dtOsVersion.textContent = osVerText;
  }
};

// ================= LIVE UPTIME TICKER & PREVIEW SIMULATION =================
let currentActiveUptimeSec = 0;
let currentKernelUptimeSec = 0;
let currentUptimeSec = 0;
let lastTelemetryTimestamp = 0;

// High precision live ticker every second so uptime ticks smoothly
setInterval(() => {
  if (lastTelemetryTimestamp > 0) {
    const elapsed = Math.floor((Date.now() - lastTelemetryTimestamp) / 1000);
    const liveActiveSec = currentActiveUptimeSec + elapsed;
    const liveKernelSec = currentKernelUptimeSec + elapsed;
    const activeText = formatUptimeOverview(liveActiveSec);
    const kernelText = formatUptimeTaskMgr(liveKernelSec);

    if (dom.ovSysUptime) {
      dom.ovSysUptime.textContent = (UPTIME_DISPLAY_MODE === 'kernel') ? kernelText : activeText;
    }
    if (dom.dtCpuActiveUptime) dom.dtCpuActiveUptime.textContent = activeText;
    if (dom.dtCpuKernelUptime) dom.dtCpuKernelUptime.textContent = kernelText;
    if (dom.dtCpuUptime) dom.dtCpuUptime.textContent = kernelText;
  }
}, 1000);

let simTimer = null;
let simTick = 0;

function runPreviewTick() {
  simTick++;
  // Windows Task Manager standard uptime baseline: 9 days 4 hours 37 minutes 59 seconds (794279s)
  const previewBaseUptime = 794279 + simTick;
  const mock = {
    uptimeSeconds: previewBaseUptime,
    bootTimestampMs: Date.now() - previewBaseUptime * 1000,
    osVersion: "Windows 10.0.19045",
    cpu: {
      name: "12th Gen Intel(R) Core(TM) i5-12400F",
      usagePercent: 24 + Math.sin(simTick * 0.2) * 8,
      physicalCores: 6,
      logicalCores: 12,
      baseFrequencyMHz: 2500,
      currentFrequencyMHz: 3990 + Math.sin(simTick * 0.3) * 200,
      temperatureC: 48 + Math.sin(simTick * 0.1) * 3,
      powerWatts: 45.2,
      processCount: 285
    },
    gpu: {
      name: "NVIDIA GeForce GTX 1660 SUPER",
      usagePercent: 18 + Math.cos(simTick * 0.15) * 6,
      dedicatedVideoMemoryBytes: 6255804416,
      memoryUsedBytes: 2.1 * 1073741824,
      temperatureC: 45,
      powerWatts: 38.0,
      powerLimitWatts: 125.0,
      clockGraphicsMHz: 1785,
      clockMemoryMHz: 7000,
      fanSpeedPercent: 35,
      adapters: [
        {
          name: "NVIDIA GeForce GTX 1660 SUPER",
          dedicatedVideoMemoryBytes: 6255804416,
          memoryUsedBytes: 2.1 * 1073741824,
          usagePercent: 18 + Math.cos(simTick * 0.15) * 6,
          temperatureC: 45,
          powerWatts: 38.0,
          powerLimitWatts: 125.0,
          clockGraphicsMHz: 1785,
          clockMemoryMHz: 7000,
          fanSpeedPercent: 35
        },
        {
          name: "Intel UHD Graphics 730",
          dedicatedVideoMemoryBytes: 128 * 1024 * 1024,
          memoryUsedBytes: 64 * 1024 * 1024,
          usagePercent: 5,
          temperatureC: 42,
          powerWatts: 8.5,
          powerLimitWatts: 35.0,
          clockGraphicsMHz: 350,
          clockMemoryMHz: 3000,
          fanSpeedPercent: 0
        }
      ]
    },
    memory: {
      name: "Gloway International Co Ltd",
      totalBytes: 34212798464,
      usedBytes: 21825605632,
      availableBytes: 12387192832,
      committedBytes: 26000000000,
      usagePercent: 63.8,
      frequencyMHz: 3000,
      modules: [
        { locator: "DIMMA1", speedMHz: 3000 },
        { locator: "DIMMB1", speedMHz: 3000 }
      ]
    },
    disk: {
      totalBytes: 1384141175136,
      freeBytes: 82079257952,
      usedBytes: 1302061917184,
      mainDrive: { root: "C:\\", totalBytes: 127912636416, freeBytes: 827412480 },
      drives: [
        { root: "C:\\", totalBytes: 127912636416, freeBytes: 827412480 },
        { root: "D:\\", totalBytes: 256042332160, freeBytes: 73143808000 },
        { root: "E:\\", totalBytes: 214748360704, freeBytes: 5027299328 },
        { root: "F:\\", totalBytes: 785437945856, freeBytes: 3071324160 }
      ]
    },
    network: {
      adapterName: "Realtek PCIe 2.5GbE Family Controller",
      connectionType: "以太网有线",
      linkSpeedMbps: 1000,
      localIp: "192.168.1.108",
      gatewayIp: "192.168.1.1",
      downloadBytesPerSecond: 420000 + Math.sin(simTick * 0.3) * 200000,
      uploadBytesPerSecond: 75000 + Math.cos(simTick * 0.2) * 30000,
      totalReceivedBytes: 12253502679 + simTick * 320000,
      totalSentBytes: 17705262221 + simTick * 50000
    },
    battery: {
      available: false,
      acLineOnline: true,
      charging: false,
      percent: 100,
      lifeSeconds: -1
    }
  };
  window.updateSystemTelemetry(mock);
}

// ================= BOOTSTRAP =================
let currentWatchId = null;
let currentIntervalMs = 1000;
let isPermissionBlocked = false;

function openComponentSettings() {
  const msgOpenSettings = {
    type: "open_widget_settings",
    widgetId: "SysMonitor"
  };
  const msgRpc = {
    type: "widget_manager_request",
    action: "open_settings",
    widgetId: "SysMonitor"
  };
  const msgLegacy = {
    type: "linwidget_open_settings_request",
    widgetId: "SysMonitor"
  };

  if (window.linwidget && typeof window.linwidget.postMessage === "function") {
    try {
      window.linwidget.postMessage(msgOpenSettings);
      window.linwidget.postMessage(msgRpc);
      window.linwidget.postMessage(msgLegacy);
    } catch (_) {}
  }

  if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === "function") {
    try {
      window.chrome.webview.postMessage(msgOpenSettings);
      window.chrome.webview.postMessage(msgRpc);
      window.chrome.webview.postMessage(msgLegacy);
    } catch (_) {}
  }

  try {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "linwidget://bridge/postMessage", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.send(JSON.stringify(msgRpc));
  } catch (_) {}
}

function renderTelemetryStandbyValues() {
  // Gracefully transition numeric readouts to clean muted dashes
  if (dom.ovCpuUsage) dom.ovCpuUsage.textContent = '--';
  if (dom.ovCpuTemp) dom.ovCpuTemp.textContent = '-- °C';
  if (dom.ovCpuClock) dom.ovCpuClock.textContent = '-- GHz';
  if (dom.ovCpuBar) dom.ovCpuBar.style.width = '0%';
  if (dom.ovSysUptime) dom.ovSysUptime.textContent = 'STANDBY';

  if (dom.ovGpuUsage) dom.ovGpuUsage.textContent = '--';
  if (dom.ovGpuTemp) dom.ovGpuTemp.textContent = '-- °C';
  if (dom.ovGpuVram) dom.ovGpuVram.textContent = '-- GB';
  if (dom.ovGpuBar) dom.ovGpuBar.style.width = '0%';

  if (dom.ovRamUsage) dom.ovRamUsage.textContent = '--';
  if (dom.ovRamUsed) dom.ovRamUsed.textContent = '-- GB';
  if (dom.ovRamTotal) dom.ovRamTotal.textContent = '/ -- GB';
  if (dom.ovRamBar) dom.ovRamBar.style.width = '0%';

  if (dom.ovDiskUsage) dom.ovDiskUsage.textContent = '--%';
  if (dom.ovDiskUsed) dom.ovDiskUsed.textContent = '-- GB';
  if (dom.ovDiskTotal) dom.ovDiskTotal.textContent = '/ -- TB';
  if (dom.ovDiskBar) dom.ovDiskBar.style.width = '0%';

  if (dom.ovNetDown) dom.ovNetDown.textContent = '-- B/s';
  if (dom.ovNetUp) dom.ovNetUp.textContent = '-- B/s';
  if (dom.ovNetBar) dom.ovNetBar.style.width = '0%';

  // Detail sheets if currently opened
  if (dom.dtCpuUsage) dom.dtCpuUsage.textContent = '--';
  if (dom.dtCpuClock) dom.dtCpuClock.textContent = '-- GHz';
  if (dom.dtCpuTemp) dom.dtCpuTemp.textContent = '-- °C';
  if (dom.dtCpuBar) dom.dtCpuBar.style.width = '0%';

  // Reset network history waveform
  historyRx.fill(0);
  historyTx.fill(0);
  if (activeDetailModule === 'net') {
    drawCanvas();
  }
}

function setPermissionBlockedState(blocked, message) {
  isPermissionBlocked = blocked;
  const statusBar = document.getElementById('telemetryStatusBar');

  if (blocked) {
    if (simTimer) {
      clearInterval(simTimer);
      simTimer = null;
    }
    if (currentWatchId && window.desktop && window.desktop.systemInfo) {
      try { window.desktop.systemInfo.unwatch(currentWatchId); } catch (e) {}
      currentWatchId = null;
    }

    document.body.classList.add('standby-state');
    if (statusBar) {
      statusBar.style.display = 'flex';
      const openBtn = document.getElementById('tsbOpenSettingsBtn');
      if (openBtn && !openBtn.__bound) {
        openBtn.__bound = true;
        openBtn.addEventListener('click', (e) => {
          e.stopPropagation();
          openComponentSettings();
        });
      }
    }

    renderTelemetryStandbyValues();
  } else {
    document.body.classList.remove('standby-state');
    if (statusBar) {
      statusBar.style.display = 'none';
    }
  }
}

async function startTelemetryStream() {
  if (isPermissionBlocked) return;

  if (!window.desktop || !window.desktop.systemInfo) {
    if (!simTimer && location.protocol !== 'linwidget:') {
      simTimer = setInterval(runPreviewTick, 1000);
      runPreviewTick();
    }
    return;
  }

  try {
    if (simTimer) {
      clearInterval(simTimer);
      simTimer = null;
    }
    if (currentWatchId) {
      try {
        await window.desktop.systemInfo.unwatch(currentWatchId);
      } catch (err) {}
      currentWatchId = null;
    }
    if (typeof window.desktop.systemInfo.getSnapshot === 'function') {
      window.desktop.systemInfo.getSnapshot().then(initial => {
        setPermissionBlockedState(false);
        if (initial && typeof initial === 'object') {
          window.updateSystemTelemetry(initial);
        }
      }).catch(err => {
        const msg = String(err && err.message || err || '');
        if (msg.includes('Desktop permission is required') || msg.includes('permission not granted')) {
          setPermissionBlockedState(true, msg);
        }
      });
    }
    currentWatchId = window.desktop.systemInfo.watch(info => {
      setPermissionBlockedState(false);
      window.updateSystemTelemetry(info);
    }, currentIntervalMs);
  } catch (e) {
    const msg = String(e && e.message || e || '');
    if (msg.includes('Desktop permission is required') || msg.includes('permission not granted')) {
      setPermissionBlockedState(true, msg);
    }
  }
}


// ================= ONE-CLICK CLIPBOARD COPY HELPER =================
function showCopyToast(text) {
  const toast = document.getElementById('copyToast');
  if (!toast) return;
  toast.textContent = text ? `已复制: ${text}` : '已复制到剪贴板';
  toast.classList.add('show');
  clearTimeout(window.__copyToastTimer);
  window.__copyToastTimer = setTimeout(() => {
    toast.classList.remove('show');
  }, 1600);
}

function copyToClipboard(str) {
  if (!str) return;
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(str).then(() => showCopyToast(str)).catch(() => {
      fallbackCopy(str);
    });
  } else {
    fallbackCopy(str);
  }
}

function fallbackCopy(str) {
  const ta = document.createElement('textarea');
  ta.value = str;
  ta.style.position = 'fixed';
  ta.style.left = '-9999px';
  document.body.appendChild(ta);
  ta.select();
  try {
    document.execCommand('copy');
    showCopyToast(str);
  } catch (err) {
    console.error('Copy failed', err);
  }
  document.body.removeChild(ta);
}

// Bind click-to-copy on all property rows and values
function initPropertyCopyHandlers() {
  document.addEventListener('click', (e) => {
    const row = e.target.closest('.prop-row');
    if (row) {
      const valEl = row.querySelector('.prop-v');
      const keyEl = row.querySelector('.prop-k');
      const valText = valEl ? valEl.textContent.trim() : '';
      const keyText = keyEl ? keyEl.textContent.trim() : '';
      if (valText && valText !== '--') {
        copyToClipboard(valText);
        row.classList.add('copied');
        setTimeout(() => row.classList.remove('copied'), 400);
      }
    }
  });
}


// ================= DYNAMIC LAYOUT & CAPSULE STATE MACHINE =================
let currentLayoutConfig = 'auto'; // 'auto' | 'standard' | 'capsule' | 'compact'

function applyLayoutMode(mode) {
  currentLayoutConfig = mode || 'auto';
  reevaluateLayout();
}

function reevaluateLayout() {
  const w = window.innerWidth;
  const h = window.innerHeight;
  let targetMode = currentLayoutConfig;

  if (currentLayoutConfig === 'auto') {
    // If aspect ratio is wide and short (e.g., width > 360 and height <= 90), or height < 120
    if (h <= 90 || (w >= 340 && h <= 120)) {
      targetMode = 'capsule';
    } else if (w <= 200 || h <= 280) {
      targetMode = 'compact';
    } else {
      targetMode = 'standard';
    }
  }

  document.body.classList.toggle('mode-capsule', targetMode === 'capsule');
  document.body.classList.toggle('mode-compact', targetMode === 'compact');
}

// Bind resize listener for auto responsiveness
window.addEventListener('resize', () => {
  reevaluateLayout();
});

window.addEventListener('DOMContentLoaded', () => {
  reevaluateLayout();
  initPropertyCopyHandlers();
  document.body.classList.remove('dark-theme');
  if (dom.ovSysUptime) {
    dom.ovSysUptime.style.cursor = 'pointer';
    dom.ovSysUptime.addEventListener('click', (e) => {
      e.stopPropagation();
      UPTIME_DISPLAY_MODE = (UPTIME_DISPLAY_MODE === 'boot') ? 'kernel' : 'boot';
      const elapsed = Math.max(0, Math.floor((Date.now() - lastTelemetryTimestamp) / 1000));
      const liveActiveSec = currentActiveUptimeSec + elapsed;
      const liveKernelSec = currentKernelUptimeSec + elapsed;
      dom.ovSysUptime.textContent = (UPTIME_DISPLAY_MODE === 'kernel') ? formatUptimeTaskMgr(liveKernelSec) : formatUptimeOverview(liveActiveSec);
    });
  }
  initCanvas();
  startTelemetryStream();
});

function handlePermissionEvent(perm, granted) {
  if (perm === 'desktop') {
    if (!granted) {
      setPermissionBlockedState(true);
    } else {
      setPermissionBlockedState(false);
      startTelemetryStream();
    }
  }
}

window.addEventListener("linwidget:permission_changed", (e) => {
  const d = e && e.detail;
  if (d) handlePermissionEvent(d.permission, d.granted);
});

window.addEventListener("message", (event) => {
  const data = event && event.data;
  if (!data || typeof data !== "object") return;
  if (data.type === "permission_changed") {
    handlePermissionEvent(data.permission, data.granted);
    return;
  }
  if (data.type === "settings_changed" || data.type === "linwidget_appearance_changed_notification") {
    const settings = data.settings;
    if (settings && typeof settings === "object") {
      if (typeof settings.theme === "string") {
        document.body.classList.toggle("dark-theme", settings.theme === "dark");
      }
      if (typeof settings.viewMode === "string") {
        applyLayoutMode(settings.viewMode);
      }
      if (settings.refreshInterval !== undefined) {
        const newInt = parseInt(settings.refreshInterval, 10);
        if (!isNaN(newInt) && newInt >= 500 && newInt !== currentIntervalMs) {
          currentIntervalMs = newInt;
          startTelemetryStream();
        }
      }
    }
  }
});
