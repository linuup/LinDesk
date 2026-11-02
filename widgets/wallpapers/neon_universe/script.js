// Neon Universe Interactive Starfield Wallpaper Script

const canvas = document.getElementById('wallpaper-canvas');
const ctx = canvas.getContext('2d');

// State configuration
let width = canvas.width = window.innerWidth;
let height = canvas.height = window.innerHeight;

let particles = [];
let targetAttractor = { x: width / 2, y: height / 2, active: false, radius: 200, force: 0.15 };
let isRunning = true;
let animationFrameId = null;

// Audio responsive buffers
let audioFrequencies = new Array(32).fill(0);
let audioVolume = 0;

// Particle Object Model
class Particle {
  constructor() {
    this.reset();
  }

  reset() {
    this.x = Math.random() * width;
    this.y = Math.random() * height;
    this.size = Math.random() * 2 + 0.5;
    this.baseSpeedX = (Math.random() - 0.5) * 0.4;
    this.baseSpeedY = (Math.random() - 0.5) * 0.4;
    this.vx = this.baseSpeedX;
    this.vy = this.baseSpeedY;
    
    // Curated cosmic palette
    const colors = [
      'rgba(59, 130, 246, ',   // Electric Blue
      'rgba(147, 51, 234, ',   // Violet
      'rgba(236, 72, 153, ',   // Neon Pink
      'rgba(6, 182, 212, '     // Cyan
    ];
    this.colorBase = colors[Math.floor(Math.random() * colors.length)];
    this.alpha = Math.random() * 0.7 + 0.3;
  }

  update() {
    // 1. Mouse Attraction / Gravity Pull
    if (targetAttractor.active) {
      const dx = targetAttractor.x - this.x;
      const dy = targetAttractor.y - this.y;
      const distance = Math.hypot(dx, dy);
      
      if (distance < targetAttractor.radius) {
        const pullStrength = (1 - distance / targetAttractor.radius) * targetAttractor.force;
        this.vx += (dx / distance) * pullStrength;
        this.vy += (dy / distance) * pullStrength;
      }
    }

    // 2. Audio-Responsive Modulation
    const speedMultiplier = 1 + (audioVolume * 6);
    this.x += this.vx * speedMultiplier;
    this.y += this.vy * speedMultiplier;

    // Apply friction to prevent infinite acceleration
    this.vx += (this.baseSpeedX - this.vx) * 0.05;
    this.vy += (this.baseSpeedY - this.vy) * 0.05;

    // Boundary wrap-around
    if (this.x < 0 || this.x > width || this.y < 0 || this.y > height) {
      this.reset();
    }
  }

  draw() {
    // Modulate size based on audio frequency band
    const band = Math.floor((this.x / width) * audioFrequencies.length);
    const audioBoost = (audioFrequencies[band] || 0) * 2;
    const finalSize = this.size + audioBoost;

    ctx.beginPath();
    ctx.arc(this.x, this.y, finalSize, 0, Math.PI * 2);
    ctx.fillStyle = `${this.colorBase}${this.alpha})`;
    ctx.fill();
  }
}

// Populate Starfield
function initParticles() {
  particles = [];
  const count = Math.floor((width * height) / 3500); // Dynamic count based on screen size
  for (let i = 0; i < Math.min(count, 800); i++) {
    particles.push(new Particle());
  }
}

// Central Loop
function loop() {
  if (!isRunning) return;

  // Fade canvas slightly for motion blur trails
  ctx.fillStyle = 'rgba(3, 7, 18, 0.08)';
  ctx.fillRect(0, 0, width, height);

  // Update and draw particles
  for (const particle of particles) {
    particle.update();
    particle.draw();
  }

  // Draw central nebula / glow at attractor
  if (targetAttractor.active) {
    const gradient = ctx.createRadialGradient(
      targetAttractor.x, targetAttractor.y, 10,
      targetAttractor.x, targetAttractor.y, targetAttractor.radius * 0.6
    );
    // Pulsate with music volume
    const alpha = (0.05 + audioVolume * 0.15).toFixed(3);
    gradient.addColorStop(0, `rgba(59, 130, 246, ${alpha})`);
    gradient.addColorStop(1, 'rgba(3, 7, 18, 0)');
    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.arc(targetAttractor.x, targetAttractor.y, targetAttractor.radius * 0.6, 0, Math.PI * 2);
    ctx.fill();
  }

  animationFrameId = requestAnimationFrame(loop);
}

// ----------------------------------------------------
// IPC Event Listeners (Receives messages from LinDesk)
// ----------------------------------------------------
window.addEventListener('message', (event) => {
  const message = event.data;
  if (!message || typeof message !== 'object') return;

  switch (message.type) {
    // A. Power Saving Commands (from C++ host)
    case 'suspend':
      isRunning = false;
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
        animationFrameId = null;
      }
      break;

    case 'resume':
      if (!isRunning) {
        isRunning = true;
        loop();
      }
      break;

    // B. Mirrored Mouse Events (from ListView layer)
    case 'desktop_mousemove':
      targetAttractor.x = message.x;
      targetAttractor.y = message.y;
      targetAttractor.active = true;
      break;

    case 'desktop_mousedown':
      targetAttractor.radius = 400; // temporary shockwave expand
      targetAttractor.force = 0.5;
      setTimeout(() => {
        targetAttractor.radius = 200;
        targetAttractor.force = 0.15;
      }, 300);
      break;

    // C. Audio Stream Spectrum (WASAPI System Loopback)
    case 'audio_data':
      if (message.frequencies && Array.isArray(message.frequencies)) {
        audioFrequencies = message.frequencies; // expecting normalized values 0.0 - 1.0
        const sum = audioFrequencies.reduce((a, b) => a + b, 0);
        audioVolume = sum / audioFrequencies.length;
      }
      break;
  }
});

// ----------------------------------------------------
// Fallback Native Event Listeners (For Browser Preview/Dev)
// ----------------------------------------------------
window.addEventListener('mousemove', (e) => {
  targetAttractor.x = e.clientX;
  targetAttractor.y = e.clientY;
  targetAttractor.active = true;
});

window.addEventListener('mousedown', () => {
  targetAttractor.radius = 400;
  targetAttractor.force = 0.5;
  setTimeout(() => {
    targetAttractor.radius = 200;
    targetAttractor.force = 0.15;
  }, 300);
});

window.addEventListener('mouseleave', () => {
  targetAttractor.active = false;
});

// Handle Window Resizing
window.addEventListener('resize', () => {
  width = canvas.width = window.innerWidth;
  height = canvas.height = window.innerHeight;
  initParticles();
});

// Ambient Clock Update
function updateClock() {
  const now = new Date();
  const timeEl = document.getElementById('clock-time');
  const dateEl = document.getElementById('clock-date');

  const hours = String(now.getHours()).padStart(2, '0');
  const minutes = String(now.getMinutes()).padStart(2, '0');
  timeEl.textContent = `${hours}:${minutes}`;

  const options = { weekday: 'long', month: 'short', day: 'numeric' };
  dateEl.textContent = now.toLocaleDateString('en-US', options);
}

// Initialization
setInterval(updateClock, 1000);
updateClock();
initParticles();
loop();
