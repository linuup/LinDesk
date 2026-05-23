const ticker = document.getElementById('ticker');
const wrap = document.querySelector('.ticker-wrap');
let headlines = [];
let currentIndex = 0;

// HackerNews RSS via rss2json
const API_URL = 'https://api.rss2json.com/v1/api.json?rss_url=https://news.ycombinator.com/rss';

async function fetchNews() {
  try {
    const res = await fetch(API_URL);
    const data = await res.json();
    if (data.items && data.items.length > 0) {
      headlines = data.items;
      if (currentIndex === 0) showNext();
    }
  } catch (err) {
    ticker.textContent = "Failed to load news.";
  }
}

function showNext() {
  if (headlines.length === 0) return;
  const item = headlines[currentIndex];
  ticker.textContent = item.title;
  ticker.onclick = () => window.open(item.link, '_blank');
  
  // Calculate animation duration based on text length to keep speed consistent
  const textWidth = ticker.offsetWidth;
  const wrapWidth = wrap.offsetWidth;
  const duration = (textWidth + wrapWidth) / 50; // 50px per second
  
  ticker.style.animation = 'none';
  ticker.offsetHeight; // trigger reflow
  ticker.style.animation = `scroll ${Math.max(duration, 5)}s linear forwards`;
  
  currentIndex = (currentIndex + 1) % headlines.length;
}

ticker.addEventListener('animationend', showNext);

// Auto-pause on hover
ticker.addEventListener('mouseenter', () => ticker.style.animationPlayState = 'paused');
ticker.addEventListener('mouseleave', () => ticker.style.animationPlayState = 'running');

fetchNews();
setInterval(fetchNews, 1000 * 60 * 15); // refresh every 15 min
