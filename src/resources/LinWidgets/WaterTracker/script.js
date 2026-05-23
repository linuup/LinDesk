const fill = document.getElementById('waterFill');
const currentEl = document.getElementById('currentAmount');
const percentEl = document.getElementById('percentAmount');
const addBtn = document.getElementById('addBtn');
const resetBtn = document.getElementById('resetBtn');

const TARGET = 2000;
const INCREMENT = 250;

let current = 0;

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
}

const cupElement = document.getElementById('cupElement');
const bubblesContainer = document.getElementById('bubblesContainer');

addBtn.addEventListener('click', () => {
  current += INCREMENT;
  saveData();
  updateUI();
  
  // Cute drinking animation
  cupElement.classList.remove('drinking');
  void cupElement.offsetWidth; // trigger reflow
  cupElement.classList.add('drinking');
  
  // Floating text
  const floatText = document.createElement('div');
  floatText.className = 'floating-text';
  floatText.textContent = `+${INCREMENT}`;
  floatText.style.left = '50%';
  floatText.style.top = '-10px';
  cupElement.appendChild(floatText);
  setTimeout(() => floatText.remove(), 1000);
  
  // Bubbles
  for(let i=0; i<4; i++) {
    const bubble = document.createElement('div');
    bubble.className = 'bubble';
    const size = Math.random() * 4 + 2;
    bubble.style.width = `${size}px`;
    bubble.style.height = `${size}px`;
    bubble.style.left = `${Math.random() * 70 + 15}%`;
    bubble.style.animationDuration = `${Math.random() * 0.5 + 0.8}s`;
    bubblesContainer.appendChild(bubble);
    setTimeout(() => bubble.remove(), 1500);
  }
});

resetBtn.addEventListener('click', () => {
  current = 0;
  saveData();
  updateUI();
});

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
