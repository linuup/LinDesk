function updateClock() {
  const now = new Date();
  
  const formatter = new Intl.DateTimeFormat(undefined, {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false
  });
  
  const parts = formatter.formatToParts(now);
  
  let hour = "", minute = "", second = "", dayPeriod = "";
  let sep1 = ":", sep2 = ":";
  
  parts.forEach(p => {
    if (p.type === 'hour') hour = p.value;
    else if (p.type === 'minute') minute = p.value;
    else if (p.type === 'second') second = p.value;
    else if (p.type === 'dayPeriod') dayPeriod = p.value;
    else if (p.type === 'literal') {
      const val = p.value.trim();
      if (val) {
        if (!minute) sep1 = val;
        else if (!second) sep2 = val;
      }
    }
  });

  const mainEl = document.getElementById("time-main");
  const secEl = document.getElementById("time-sec");
  const ampmEl = document.getElementById("time-ampm");
  
  if (mainEl) mainEl.textContent = `${hour}${sep1}${minute}`;
  if (secEl) secEl.textContent = `${sep2}${second}`;
  if (ampmEl) {
    if (dayPeriod) {
      ampmEl.style.display = "";
      ampmEl.textContent = ` ${dayPeriod}`;
    } else {
      ampmEl.style.display = "none";
      ampmEl.textContent = "";
    }
  }

  const dateFullStr = new Intl.DateTimeFormat(undefined, {
    weekday: "long",
    month: "long",
    day: "numeric"
  }).format(now);
  
  const dateShortStr = new Intl.DateTimeFormat(undefined, {
    weekday: "short",
    month: "short",
    day: "numeric"
  }).format(now);
  
  const dateFullEl = document.getElementById("date-full");
  if (dateFullEl) dateFullEl.textContent = dateFullStr;

  const dateShortEl = document.getElementById("date-short");
  if (dateShortEl) dateShortEl.textContent = dateShortStr;

  // Analog Clock Update
  const hourHand = document.getElementById("hourHand");
  const minuteHand = document.getElementById("minuteHand");
  const secondHand = document.getElementById("secondHand");
  
  if (hourHand) {
    const hours = now.getHours() % 12;
    const minutes = now.getMinutes();
    const seconds = now.getSeconds();
    
    const secDeg = (seconds / 60) * 360;
    const minDeg = ((minutes + seconds / 60) / 60) * 360;
    const hourDeg = ((hours + minutes / 60) / 12) * 360;
    
    hourHand.style.transform = `rotate(${hourDeg}deg)`;
    minuteHand.style.transform = `rotate(${minDeg}deg)`;
    secondHand.style.transform = `rotate(${secDeg}deg)`;
  }
}

updateClock();
setInterval(updateClock, 1000);

// Theme Toggle Logic
function bindThemeToggle() {
  const themeToggle = document.getElementById("themeToggle");
  const currentTheme = localStorage.getItem("clockTheme") || "dark";

  if (currentTheme === "light") {
    document.body.classList.add("light-theme");
    themeToggle.textContent = "🌙";
  } else {
    themeToggle.textContent = "☀️";
  }

  themeToggle.addEventListener("click", () => {
    document.body.classList.toggle("light-theme");
    const isLight = document.body.classList.contains("light-theme");
    localStorage.setItem("clockTheme", isLight ? "light" : "dark");
    themeToggle.textContent = isLight ? "🌙" : "☀️";
  });
}

bindThemeToggle();

// Style Switcher Logic
function bindStyleSwitcher() {
  const styleDots = document.querySelectorAll('.style-dot');
  let currentStyle = localStorage.getItem('clockStyle') || 'digital';

  function setStyle(style) {
    currentStyle = style;
    if (style === 'analog') {
      document.body.classList.add('style-analog');
    } else {
      document.body.classList.remove('style-analog');
    }
    
    styleDots.forEach(dot => {
      if (dot.getAttribute('data-style') === style) {
        dot.classList.add('active');
      } else {
        dot.classList.remove('active');
      }
    });
    
    localStorage.setItem('clockStyle', style);
  }

  setStyle(currentStyle);

  styleDots.forEach(dot => {
    dot.addEventListener('click', (e) => {
      e.stopPropagation();
      setStyle(dot.getAttribute('data-style'));
    });
  });
}

bindStyleSwitcher();

// Analog Clock Markers Init
function initAnalogClock() {
  const container = document.getElementById("clockMarkers");
  if (!container) return;
  container.innerHTML = "";
  for (let i = 0; i < 30; i++) {
    const marker = document.createElement("div");
    marker.className = "marker";
    if (i % 5 === 0) {
      marker.classList.add("main");
      if (i === 0 || i === 15) {
        marker.classList.add("cardinal");
      }
    } else {
      marker.classList.add("minor");
    }
    marker.style.transform = `rotate(${i * 6}deg)`;
    container.appendChild(marker);
  }
  
  // Draw 12 numbers
  for (let i = 1; i <= 12; i++) {
    const numWrap = document.createElement("div");
    numWrap.className = "number-wrap";
    numWrap.style.transform = `rotate(${i * 30}deg)`;
    
    const num = document.createElement("div");
    num.className = "number";
    num.style.transform = `rotate(-${i * 30}deg)`;
    num.textContent = i;
    
    numWrap.appendChild(num);
    container.appendChild(numWrap);
  }
}
initAnalogClock();
