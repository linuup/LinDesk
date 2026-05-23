const blades = document.getElementById('blades');
const btn = document.getElementById('toggleBtn');
let speed = 0; // 0, 1, 2, 3

// Animation durations in seconds. 
// Note: We use linear timing function via JS to keep rotation smooth, 
// but CSS initially uses cubic-bezier. We'll set it to linear when running.
const speeds = [0, 0.8, 0.3, 0.1]; 

btn.addEventListener('click', () => {
  speed = (speed + 1) % 4;
  updateFan();
});

function updateFan() {
  if (speed === 0) {
    blades.style.animationDuration = '0s';
    blades.style.animationTimingFunction = 'ease-out';
  } else {
    blades.style.animationDuration = speeds[speed] + 's';
    blades.style.animationTimingFunction = 'linear';
  }
  
  // Update LEDs
  document.getElementById('led1').className = 'led' + (speed >= 1 ? ' active' : '');
  document.getElementById('led2').className = 'led' + (speed >= 2 ? ' active' : '');
  document.getElementById('led3').className = 'led' + (speed >= 3 ? ' active' : '');
}

const fan = document.querySelector('.fan-container');
const controls = document.querySelector('.controls');
const widget = document.querySelector('.widget');

function resizeLayout() {
  const h = widget.clientHeight;
  const w = widget.clientWidth;
  
  if (w > h * 1.5) {
     widget.style.flexDirection = 'row';
     controls.style.flexDirection = 'column';
  } else {
     widget.style.flexDirection = 'column';
     controls.style.flexDirection = 'row';
  }
  
  const availH = widget.style.flexDirection === 'row' ? h - 20 : h - controls.clientHeight - 30;
  const availW = widget.style.flexDirection === 'row' ? w - controls.clientWidth - 30 : w - 20;
  const size = Math.min(availH, availW);
  const scale = size / 120;
  if (scale > 0.1) {
    fan.style.transform = `scale(${Math.min(scale, 2.5)})`;
  }
}
window.addEventListener('resize', resizeLayout);
setTimeout(resizeLayout, 100);
