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
