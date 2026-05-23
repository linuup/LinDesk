const priceEl = document.getElementById('currentPrice');
const changeEl = document.getElementById('changePercent');
const canvas = document.getElementById('trendCanvas');
let ctx = null;
let history = [];

let currentSymbol = 'BTC-USDT';
let currentId = 'bitcoin';

const chips = document.querySelectorAll('.coin-chip');
chips.forEach(chip => {
  chip.addEventListener('click', () => {
    chips.forEach(c => c.classList.remove('active'));
    chip.classList.add('active');
    currentSymbol = chip.getAttribute('data-symbol');
    currentId = chip.getAttribute('data-id');
    
    // Reset data
    history = [];
    priceEl.textContent = 'Loading...';
    changeEl.textContent = '--%';
    changeEl.className = 'change';
    if(ctx) ctx.clearRect(0, 0, canvas.width, canvas.height);
    
    fetchCrypto();
  });
});

function resize() {
  const rect = canvas.parentElement.getBoundingClientRect();
  if(rect.width === 0) return;
  canvas.width = rect.width * window.devicePixelRatio;
  canvas.height = rect.height * window.devicePixelRatio;
  ctx = canvas.getContext('2d');
  draw();
}
window.addEventListener('resize', resize);

function draw() {
  if (!ctx || history.length < 2) return;
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  
  const min = Math.min(...history);
  const max = Math.max(...history);
  const range = (max - min) || 1;
  const isUp = history[history.length-1] >= history[0];
  const color = isUp ? '#10b981' : '#ef4444';
  const fillAlpha = isUp ? 'rgba(16, 185, 129, 0.15)' : 'rgba(239, 68, 68, 0.15)';
  
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2 * window.devicePixelRatio;
  ctx.lineJoin = 'round';
  
  for(let i=0; i<history.length; i++) {
    const x = (i / (history.length - 1)) * w;
    const y = h - ((history[i] - min) / range) * h * 0.8 - (h * 0.1);
    if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
  }
  ctx.stroke();

  // Fill gradient
  ctx.lineTo(w, h);
  ctx.lineTo(0, h);
  ctx.fillStyle = fillAlpha;
  ctx.fill();
}

async function fetchCrypto() {
  const sym = currentSymbol;
  const id = currentId;
  try {
    const res = await fetch(`https://api.kucoin.com/api/v1/market/stats?symbol=${sym}`);
    const json = await res.json();
    if (!json.data) throw new Error("Invalid KuCoin response");
    if (sym !== currentSymbol) return; // Switched during fetch
    
    const price = parseFloat(json.data.last);
    const change = parseFloat(json.data.changeRate) * 100;
    
    updateUI(price, change, sym);
  } catch (err1) {
    try {
      const res = await fetch(`https://api.coingecko.com/api/v3/simple/price?ids=${id}&vs_currencies=usd&include_24hr_change=true`);
      const json = await res.json();
      if (!json[id]) throw new Error("Invalid CoinGecko response");
      if (sym !== currentSymbol) return; // Switched during fetch
      
      const price = parseFloat(json[id].usd);
      const change = parseFloat(json[id].usd_24h_change);
      
      updateUI(price, change, sym);
    } catch (err2) {
      if (sym === currentSymbol && history.length === 0) priceEl.textContent = "API Error";
    }
  }
}

function updateUI(price, change, sym) {
  if (sym !== currentSymbol) return;
  const decimals = price < 2 ? 4 : 2; // more decimals for cheaper coins like DOGE
  priceEl.textContent = price.toLocaleString('en-US', {minimumFractionDigits: decimals, maximumFractionDigits: decimals});
  changeEl.textContent = (change >= 0 ? '+' : '') + change.toFixed(2) + '%';
  changeEl.className = 'change ' + (change >= 0 ? 'up' : 'down');
  
  history.push(price);
  if (history.length > 30) history.shift();
  draw();
}

setTimeout(resize, 100);
fetchCrypto();
setInterval(fetchCrypto, 5000);
