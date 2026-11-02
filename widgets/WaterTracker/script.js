const fill = document.getElementById('waterBg');
const avatar = document.getElementById('avatar');
const speechBubble = document.getElementById('speechBubble');
const currentEl = document.getElementById('currentAmount');
const percentEl = document.getElementById('percentAmount');
const addBtn = document.getElementById('addBtn');
const resetBtn = document.getElementById('resetBtn');
const drinkTip = document.getElementById('drinkTip');

const TARGET = 2000;
const INCREMENT = 250;
let current = 0;

const TIPS = [
  "Time to drink some water! 💧",
  "Stay fresh, stay hydrated! ✨",
  "Your body will thank you! ❤️",
  "Glug glug glug! 🥛",
  "Keep up the great work! 🎉",
  "Hydration is key! 🌟",
  "Water = Energy! ⚡"
];

const REMINDERS = [
  "Thirsty... 🥺",
  "Water please! 💧",
  "Time for a sip! ☕",
  "Don't forget to drink! ✨",
  "Glug glug? 🥛",
  "Feed me water! 🌊"
];

const HAPPY_PHRASES = [
  "Yum! 🥰",
  "Refreshing! ☀️",
  "Thank you! ❤️",
  "Delicious! 🍭",
  "Gulp gulp! 🎉"
];

let speechTimer = null;
let reminderInterval = null;

function showSpeech(text, duration = 3000) {
  clearTimeout(speechTimer);
  speechBubble.textContent = text;
  speechBubble.classList.add('show');
  
  speechTimer = setTimeout(() => {
    speechBubble.classList.remove('show');
  }, duration);
}

function startReminderLoop() {
  clearInterval(reminderInterval);
  reminderInterval = setInterval(() => {
    const p = Math.min((current / TARGET) * 100, 100);
    if (p < 100) {
      const txt = REMINDERS[Math.floor(Math.random() * REMINDERS.length)];
      showSpeech(txt);
    } else {
      if (Math.random() > 0.7) {
        showSpeech("I'm full! 😋");
      }
    }
  }, 14000);
}

function loadData() {
  const data = JSON.parse(localStorage.getItem('lin_water_tracker') || '{}');
  const today = new Date().toDateString();
  if (data.date === today) {
    current = data.amount || 0;
  } else {
    current = 0;
    saveData();
  }
  updateUI();
  startReminderLoop();
  scheduleNextSwim();
}

function saveData() {
  const today = new Date().toDateString();
  localStorage.setItem('lin_water_tracker', JSON.stringify({ date: today, amount: current }));
}

function updateUI() {
  const p = Math.min((current / TARGET) * 100, 100);
  fill.style.height = `${p}%`;
  currentEl.textContent = current;
  percentEl.textContent = `${Math.round(p)}%`;
  
  // Calculate scale factor: 1.0 at 0% water, up to 1.8x at 100% water
  const scale = 1 + (p / 100) * 0.8;
  avatar.style.setProperty('--droplet-scale', scale);
  
  // Expressions states
  if (p >= 100) {
    avatar.classList.add('full');
    avatar.classList.remove('dry');
  } else if (p < 15) {
    avatar.classList.add('dry');
    avatar.classList.remove('full');
  } else {
    avatar.classList.remove('dry');
    avatar.classList.remove('full');
  }
  
  // Update tip randomly on load/reset
  if (current === 0) {
    drinkTip.textContent = "Please feed me some water! 🥺";
  }
}

// Drag & Physics Variables
let isDragging = false;
let startX = 0;
let hasDragged = false;
let avatarLeftPercent = 50; // default center
let targetLeftPercent = 50;
let animFrameId = null;

avatar.style.left = `${avatarLeftPercent}%`;

avatar.addEventListener('mousedown', (e) => {
  isDragging = true;
  hasDragged = false;
  avatar.classList.add('dragging');
  startX = e.clientX;
  if (!animFrameId) updatePhysics();
});

document.addEventListener('mousemove', (e) => {
  if (!isDragging) return;
  if (Math.abs(e.clientX - startX) > 5) {
    hasDragged = true;
  }
  const widgetRect = document.getElementById('drinkWidget').getBoundingClientRect();
  let relX = e.clientX - widgetRect.left;
  let pct = (relX / widgetRect.width) * 100;
  targetLeftPercent = Math.max(8, Math.min(pct, 92));
});

document.addEventListener('mouseup', () => {
  if (!isDragging) return;
  isDragging = false;
  avatar.classList.remove('dragging');
});

// Touch support
avatar.addEventListener('touchstart', (e) => {
  isDragging = true;
  hasDragged = false;
  avatar.classList.add('dragging');
  startX = e.touches[0].clientX;
  if (!animFrameId) updatePhysics();
});

document.addEventListener('touchmove', (e) => {
  if (!isDragging) return;
  if (Math.abs(e.touches[0].clientX - startX) > 5) {
    hasDragged = true;
  }
  const widgetRect = document.getElementById('drinkWidget').getBoundingClientRect();
  let relX = e.touches[0].clientX - widgetRect.left;
  let pct = (relX / widgetRect.width) * 100;
  targetLeftPercent = Math.max(8, Math.min(pct, 92));
});

document.addEventListener('touchend', () => {
  if (!isDragging) return;
  isDragging = false;
  avatar.classList.remove('dragging');
});

function updatePhysics() {
  const lerpFactor = isDragging ? 0.16 : 0.025; // Slow, lazy swim; fast, responsive drag
  
  if (!isDragging && Math.abs(targetLeftPercent - avatarLeftPercent) < 0.05) {
    avatarLeftPercent = targetLeftPercent;
    avatar.style.left = `${avatarLeftPercent}%`;
    avatar.style.transform = `translateX(-50%)`;
    animFrameId = null;
    return;
  }
  
  const prevLeft = avatarLeftPercent;
  avatarLeftPercent += (targetLeftPercent - avatarLeftPercent) * lerpFactor;
  
  const velocity = avatarLeftPercent - prevLeft;
  // Boost velocity visualization during slow swim so deformation is visible
  const displayVelocity = isDragging ? velocity : velocity * 3.5;
  const stretchX = 1 + Math.abs(displayVelocity) * 0.045;
  const stretchY = 1 - Math.abs(displayVelocity) * 0.035;
  
  avatar.style.left = `${avatarLeftPercent}%`;
  avatar.style.transform = `translateX(-50%) scale(${stretchX}, ${stretchY})`;
  
  animFrameId = requestAnimationFrame(updatePhysics);
}

// Actions & Click events
addBtn.addEventListener('click', (e) => {
  current += INCREMENT;
  saveData();
  updateUI();
  
  // Clear any ongoing idle animations
  clearTimeout(idleTimer);
  ANIMS.forEach(anim => avatar.classList.remove(anim));
  
  // Trigger cute drinking animation & speech
  avatar.classList.add('happy');
  drinkTip.textContent = TIPS[Math.floor(Math.random() * TIPS.length)];
  
  const p = Math.min((current / TARGET) * 100, 100);
  if (p >= 100) {
    showSpeech("Goal reached! 🎉 I'm full! 😋", 4000);
  } else {
    showSpeech(HAPPY_PHRASES[Math.floor(Math.random() * HAPPY_PHRASES.length)]);
  }
  
  setTimeout(() => {
    avatar.classList.remove('happy');
    scheduleNextIdle(); // resume idle cycles
  }, 1200);
  
  // Create beautiful bubble particles popping up
  createParticlesAt(addBtn, ['💧', '🫧']);
});

resetBtn.addEventListener('click', () => {
  current = 0;
  saveData();
  updateUI();
  drinkTip.textContent = "Please feed me some water! 🥺";
  showSpeech("Reset! Empty... 🥤");
});

// Click avatar to trigger random animation manually (unless they were dragging)
avatar.addEventListener('click', () => {
  if (hasDragged) return;
  
  clearTimeout(idleTimer);
  ANIMS.forEach(anim => avatar.classList.remove(anim));
  
  const randomAnim = ANIMS[Math.floor(Math.random() * ANIMS.length)];
  avatar.classList.add(randomAnim);
  
  // Speech on poke
  const pokePhrases = ["Hei! 🖐️", "Tickles! 😂", "Whoa! 🌀", "Boing! 🍮", "Hello! ✨"];
  showSpeech(pokePhrases[Math.floor(Math.random() * pokePhrases.length)]);
  
  setTimeout(() => {
    avatar.classList.remove(randomAnim);
    scheduleNextIdle();
  }, 1200);
  
  createParticlesAt(avatar, ['💖', '✨', '⭐', '🌈']);
});

// Random idle animations
const ANIMS = ['anim-blink', 'anim-wobble', 'anim-hop', 'anim-look', 'anim-roll'];
let idleTimer = null;

function triggerRandomIdle() {
  // Only trigger if not happy, dry, full, or dragging
  if (avatar.classList.contains('happy') || avatar.classList.contains('dry') || avatar.classList.contains('dragging')) {
    scheduleNextIdle();
    return;
  }
  
  const randomAnim = ANIMS[Math.floor(Math.random() * ANIMS.length)];
  avatar.classList.add(randomAnim);
  
  setTimeout(() => {
    avatar.classList.remove(randomAnim);
    scheduleNextIdle();
  }, 1500);
}

function scheduleNextIdle() {
  clearTimeout(idleTimer);
  const delay = Math.random() * 4000 + 4000;
  idleTimer = setTimeout(triggerRandomIdle, delay);
}

// Random Swim behavior
let swimTimer = null;

function triggerRandomSwim() {
  // Only swim if not dragging, not dry, not happy
  if (isDragging || avatar.classList.contains('dry') || avatar.classList.contains('happy')) {
    scheduleNextSwim();
    return;
  }
  
  // Pick random target percent on the horizontal axis (between 15% and 85%)
  targetLeftPercent = 15 + Math.random() * 70;
  
  // Trigger update loop
  if (!animFrameId) {
    updatePhysics();
  }
  
  // Occasionally say something cute while swimming
  if (Math.random() > 0.75) {
    const swimPhrases = ["Glug glug... 🌊", "Cruising~ ⛵", "Lovely water! 🫧", "Wheee! 💨"];
    showSpeech(swimPhrases[Math.floor(Math.random() * swimPhrases.length)], 2000);
  }
  
  scheduleNextSwim();
}

function scheduleNextSwim() {
  clearTimeout(swimTimer);
  // Random delay between 8 and 15 seconds
  const delay = Math.random() * 7000 + 8000;
  swimTimer = setTimeout(triggerRandomSwim, delay);
}

function createParticlesAt(element, emojis = ['💧', '🫧']) {
  const rect = element.getBoundingClientRect();
  const x = rect.left + rect.width / 2;
  const y = rect.top + rect.height / 2;
  
  for(let i=0; i<8; i++) {
    const particle = document.createElement('div');
    particle.className = 'particle';
    particle.textContent = emojis[Math.floor(Math.random() * emojis.length)];
    particle.style.left = `${x + (Math.random() * 30 - 15)}px`;
    particle.style.top = `${y + (Math.random() * 20 - 10)}px`;
    
    const dx = (Math.random() * 80 - 40) + 'px';
    const dy = (-30 - Math.random() * 60) + 'px';
    particle.style.setProperty('--dx', dx);
    particle.style.setProperty('--dy', dy);
    
    document.body.appendChild(particle);
    setTimeout(() => particle.remove(), 1000);
  }
}

// Check for day change every hour to reset automatically
setInterval(() => {
  const today = new Date().toDateString();
  const data = JSON.parse(localStorage.getItem('lin_water_tracker') || '{}');
  if (data.date && data.date !== today) {
    current = 0;
    saveData();
    updateUI();
  }
}, 3600000);

loadData();
