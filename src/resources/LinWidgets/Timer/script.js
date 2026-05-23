const widget = document.getElementById('timerWidget');
const timeInput = document.getElementById('timeInput');
const actionBtn = document.getElementById('actionBtn');
const audio = document.getElementById('timeEndAudio');

const iconPlay = document.getElementById('iconPlay');
const iconPause = document.getElementById('iconPause');
const iconStop = document.getElementById('iconStop');

const themeToggle = document.getElementById('themeToggle');
const iconSun = document.getElementById('iconSun');
const iconMoon = document.getElementById('iconMoon');

const contextMenu = document.getElementById('contextMenu');
const menuItems = document.querySelectorAll('.menu-item');

let totalSeconds = 5 * 60; // Default 5 minutes
const savedDuration = localStorage.getItem('lindesk_timer_duration');
if (savedDuration && !isNaN(parseInt(savedDuration))) {
  totalSeconds = parseInt(savedDuration, 10);
}

let remainingSeconds = totalSeconds;
let state = 'Ready'; // Ready, Running, Paused, Done
let targetEndTime = 0;

// Worker Setup (高频 tick，保证休眠不被节流)
const workerBlob = new Blob([`
  let timerId = null;
  self.onmessage = function(e) {
    if (e.data === 'start') {
      if (timerId) clearInterval(timerId);
      timerId = setInterval(() => postMessage('tick'), 30);
    } else if (e.data === 'stop') {
      clearInterval(timerId);
      timerId = null;
    }
  };
`], { type: 'application/javascript' });
const timerWorker = new Worker(URL.createObjectURL(workerBlob));

timerWorker.onmessage = function(e) {
  if (e.data === 'tick' && state === 'Running') {
    const now = Date.now();
    const exactRemaining = Math.max(0, (targetEndTime - now) / 1000);
    
    // 更新文本 (整数变化时)
    const currentInt = Math.ceil(exactRemaining);
    if (currentInt !== remainingSeconds) {
      remainingSeconds = currentInt;
      timeInput.value = formatTime(remainingSeconds);
    }
    
    // 更新环形进度条 (毫秒级丝滑进度)
    if (ringProgress && totalSeconds > 0) {
      const progress = exactRemaining / totalSeconds;
      const offset = circumference * (1 - progress);
      ringProgress.style.strokeDashoffset = offset;
    }
    
    // 结束处理
    if (exactRemaining <= 0) {
      remainingSeconds = 0;
      timeInput.value = formatTime(0);
      timerWorker.postMessage('stop');
      setState('Done');
      if (ringProgress) ringProgress.style.strokeDashoffset = 0; // 填满圆环，配合 CSS 动效
    }
  }
};

// 主题切换逻辑
let isLight = localStorage.getItem('lindesk_timer_theme') === 'light';
if (isLight) {
  document.body.classList.add('light-theme');
  iconSun.style.display = 'none';
  iconMoon.style.display = 'block';
} else {
  iconSun.style.display = 'block';
  iconMoon.style.display = 'none';
}

themeToggle.addEventListener('click', () => {
  isLight = !isLight;
  if (isLight) {
    document.body.classList.add('light-theme');
    iconSun.style.display = 'none';
    iconMoon.style.display = 'block';
  } else {
    document.body.classList.remove('light-theme');
    iconSun.style.display = 'block';
    iconMoon.style.display = 'none';
  }
  localStorage.setItem('lindesk_timer_theme', isLight ? 'light' : 'dark');
});

function playSound() {
  audio.currentTime = 0;
  audio.loop = true;
  audio.play().catch(e => console.log('Audio play blocked:', e));
}

function stopSound() {
  audio.pause();
  audio.currentTime = 0;
}

// Format seconds to MM:SS
function formatTime(sec) {
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}

// Parse user input (supports "5" -> 5m, "1:30" -> 1m 30s)
function parseTime(str) {
  const parts = str.split(':');
  if (parts.length === 2) {
    const m = parseInt(parts[0], 10) || 0;
    const s = parseInt(parts[1], 10) || 0;
    return m * 60 + s;
  }
  const val = parseInt(str, 10);
  if (!isNaN(val)) return val * 60; 
  return totalSeconds; 
}

// Input handling
timeInput.addEventListener('blur', () => {
  if (state === 'Running') return; 
  const newSec = parseTime(timeInput.value);
  totalSeconds = Math.max(1, newSec); // at least 1 sec
  remainingSeconds = totalSeconds;
  localStorage.setItem('lindesk_timer_duration', totalSeconds); // 持久化保存
  
  timeInput.value = formatTime(remainingSeconds);
  if (state === 'Done') {
    setState('Ready');
  }
});

timeInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    timeInput.blur();
  }
});

timeInput.addEventListener('focus', () => {
  if (state === 'Running') {
    timeInput.blur(); // No edit while running
  }
});

const ringProgress = document.getElementById('ringProgress');
const circumference = 2 * Math.PI * 46;

function updateDisplay() {
  timeInput.value = formatTime(remainingSeconds);
  
  if (ringProgress) {
    if (totalSeconds > 0) {
      const progress = remainingSeconds / totalSeconds;
      const offset = circumference * (1 - progress);
      ringProgress.style.strokeDashoffset = offset;
    } else {
      ringProgress.style.strokeDashoffset = 0;
    }
  }
}

function setState(newState) {
  if (state === 'Done' && newState !== 'Done') {
    stopSound();
  }
  
  widget.className = `widget state-${newState.toLowerCase()}`;
  state = newState;
  
  iconPlay.style.display = 'none';
  iconPause.style.display = 'none';
  iconStop.style.display = 'none';
  
  timeInput.readOnly = (state === 'Running');

  if (state === 'Ready') {
    iconPlay.style.display = 'block';
  } else if (state === 'Running') {
    iconPause.style.display = 'block';
  } else if (state === 'Paused') {
    iconPlay.style.display = 'block';
  } else if (state === 'Done') {
    iconStop.style.display = 'block';
    playSound();
  }
}

actionBtn.addEventListener('click', () => {
  if (state === 'Ready' || state === 'Paused') {
    targetEndTime = Date.now() + remainingSeconds * 1000;
    setState('Running');
    timerWorker.postMessage('start');
  } else if (state === 'Running') {
    setState('Paused');
    timerWorker.postMessage('stop');
    remainingSeconds = Math.ceil(Math.max(0, (targetEndTime - Date.now()) / 1000));
    updateDisplay();
  }
});

// 全局点击拦截：当计时结束(Done)时，任意点击都会停止声音并重置状态
widget.addEventListener('click', (e) => {
  if (state === 'Done') {
    // 停止声音并重置回Ready
    setState('Ready');
    remainingSeconds = totalSeconds;
    updateDisplay();
  }
});

// ---------------- 右键快捷菜单逻辑 ----------------

// 点击空白处隐藏菜单
document.addEventListener('click', (e) => {
  if (e.button !== 2) {
    contextMenu.classList.remove('show');
  }
});

// 监听右键点击
widget.addEventListener('contextmenu', (e) => {
  e.preventDefault(); // 阻止原生右键菜单
  
  if (state === 'Running') return; // 运行中不允许通过右键更改时间
  
  // 确保菜单不超出窗口边界
  let x = e.clientX;
  let y = e.clientY;
  
  // 预先显示以获取真实宽高
  contextMenu.style.visibility = 'hidden';
  contextMenu.classList.add('show');
  const rect = contextMenu.getBoundingClientRect();
  
  if (x + rect.width > window.innerWidth) {
    x = window.innerWidth - rect.width - 5;
  }
  if (y + rect.height > window.innerHeight) {
    y = window.innerHeight - rect.height - 5;
  }
  
  contextMenu.style.left = `${x}px`;
  contextMenu.style.top = `${y}px`;
  
  // 恢复可见性
  contextMenu.style.visibility = '';
});

// 监听菜单项点击
menuItems.forEach(item => {
  item.addEventListener('click', () => {
    const min = parseInt(item.getAttribute('data-time'), 10);
    if (!isNaN(min)) {
      totalSeconds = min * 60;
      remainingSeconds = totalSeconds;
      localStorage.setItem('lindesk_timer_duration', totalSeconds); // 持久化保存
      setState('Ready');
      updateDisplay();
    }
    contextMenu.classList.remove('show');
  });
});

// ---------------- 样式切换逻辑 (Style Switcher) ----------------
const styleDots = document.querySelectorAll('.style-dot');
let currentStyle = localStorage.getItem('lindesk_timer_style') || 'neon';

function setStyle(style) {
  currentStyle = style;
  if (style === 'fluent') {
    document.body.classList.add('style-fluent');
  } else {
    document.body.classList.remove('style-fluent');
  }
  
  styleDots.forEach(dot => {
    if (dot.getAttribute('data-style') === style) {
      dot.classList.add('active');
    } else {
      dot.classList.remove('active');
    }
  });
  
  localStorage.setItem('lindesk_timer_style', style);
}

// 初始化样式
setStyle(currentStyle);

styleDots.forEach(dot => {
  dot.addEventListener('click', (e) => {
    e.stopPropagation();
    setStyle(dot.getAttribute('data-style'));
  });
});

// 初始化显示
updateDisplay();
