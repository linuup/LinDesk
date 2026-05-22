const defaultLocation = {
  latitude: 31.2304,
  longitude: 121.4737,
  label: "Shanghai"
};

const weatherCodeMap = {
  0: { icon: "☀️", label: "晴朗" },
  1: { icon: "🌤️", label: "主要晴间多云" },
  2: { icon: "⛅", label: "多云" },
  3: { icon: "☁️", label: "阴天" },
  45: { icon: "🌫️", label: "雾" },
  48: { icon: "🌫️", label: "冻雾" },
  51: { icon: "🌦️", label: "细雨" },
  53: { icon: "🌦️", label: "中雨" },
  55: { icon: "🌧️", label: "毛毛雨" },
  56: { icon: "🌧️", label: "冰雨" },
  57: { icon: "🌧️", label: "冻雨" },
  61: { icon: "🌧️", label: "小雨" },
  63: { icon: "🌧️", label: "中雨" },
  65: { icon: "🌧️", label: "大雨" },
  66: { icon: "🌧️", label: "小冰雹" },
  67: { icon: "🌧️", label: "大冰雹" },
  71: { icon: "❄️", label: "降雪" },
  73: { icon: "❄️", label: "轻雪" },
  75: { icon: "❄️", label: "大雪" },
  77: { icon: "❄️", label: "冰粒" },
  80: { icon: "🌧️", label: "阵雨" },
  81: { icon: "🌧️", label: "强阵雨" },
  82: { icon: "⛈️", label: "暴雨" },
  85: { icon: "❄️", label: "小雪" },
  86: { icon: "❄️", label: "大雪" },
  95: { icon: "⛈️", label: "雷阵雨" },
  96: { icon: "⛈️", label: "雷阵雨带冰雹" },
  99: { icon: "⛈️", label: "强雷阵雨" }
};

const elements = {
  icon: document.getElementById("icon"),
  temperature: document.getElementById("temperature"),
  condition: document.getElementById("condition"),
  aqi: document.getElementById("aqi"),
  humidity: document.getElementById("humidity"),
  wind: document.getElementById("wind"),
  locationInput: document.getElementById("locationInput")
};

function formatTime(date) {
  return new Intl.DateTimeFormat(undefined, {
    hour: "2-digit",
    minute: "2-digit"
  }).format(date);
}

function mapWeatherCode(code) {
  return weatherCodeMap[code] || { icon: "🌈", label: "未知天气" };
}

function setStatus({ city, icon, temp, cond, humidity, wind, aqi }) {
  if (document.activeElement !== elements.locationInput) {
    elements.locationInput.value = city;
  }
  elements.icon.textContent = icon;
  elements.temperature.textContent = `${Math.round(temp)}°`;
  elements.condition.textContent = cond;
  elements.aqi.textContent = aqi;
  elements.humidity.textContent = humidity;
  elements.wind.textContent = wind;
}

function setError(message) {
  setStatus({
    city: "Weather Mini",
    icon: "⚠️",
    temp: 0,
    cond: message,
    humidity: "--%",
    wind: "-- km/h",
    aqi: "--"
  });
}

function getSavedLocationName() {
  return localStorage.getItem("weatherLocation") || "";
}

function saveLocationName(name) {
  localStorage.setItem("weatherLocation", name);
}

async function geocodeLocation(query) {
  if (!query) return null;
  try {
    // 针对国内地名进行优化：去掉尾部的"省,市,区,县,新区"，提高开源API的命中率
    let optimizedQuery = query.replace(/(省|市|区|县|新区)$/g, '');
    // 将中间的"省"或"市"替换为空格，例如 "北京市海淀" -> "北京 海淀"
    optimizedQuery = optimizedQuery.replace(/(省|市)/g, ' ');
    
    const url = `https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(optimizedQuery)}&count=1&language=zh&format=json`;
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error("地名检索失败");
    }
    const data = await response.json();
    if (data.results && data.results.length > 0) {
      const place = data.results[0];
      return {
        latitude: place.latitude,
        longitude: place.longitude,
        label: `${place.name}${place.admin1 ? ", " + place.admin1 : ""}${place.country ? ", " + place.country : ""}`
      };
    }
  } catch (error) {
    console.warn("地名检索失败：", error);
  }
  return null;
}

async function getLocation() {
  const savedName = getSavedLocationName();
  if (savedName) {
    const geocoded = await geocodeLocation(savedName);
    if (geocoded) {
      return geocoded;
    }
  }

  try {
    const response = await fetch("https://geolocation-db.com/json/");
    if (!response.ok) {
      throw new Error("定位服务失败");
    }
    const data = await response.json();
    if (data.latitude && data.longitude) {
      return {
        latitude: Number(data.latitude),
        longitude: Number(data.longitude),
        label: `${data.city || "未知城市"}${data.country_code ? ", " + data.country_code : ""}`
      };
    }
  } catch (error) {
    console.warn("定位失败：", error);
  }
  return defaultLocation;
}

async function fetchWeather(latitude, longitude) {
  const url = `https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}&current_weather=true&hourly=relativehumidity_2m&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto`;
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error("天气服务失败");
  }
  return response.json();
}

async function updateWeather(locationQuery) {
  try {
    let location;
    if (locationQuery) {
      location = await geocodeLocation(locationQuery);
    }
    location = location || await getLocation();

    const weather = await fetchWeather(location.latitude, location.longitude);
    const current = weather.current_weather;
    if (!current) {
      throw new Error("当前天气数据缺失");
    }

    const mapped = mapWeatherCode(current.weathercode);
    const humidity = weather.hourly?.relativehumidity_2m?.[0] != null ? `${Math.round(weather.hourly.relativehumidity_2m[0])}%` : "--%";
    const wind = `${Math.round(current.windspeed)} km/h`;

    setStatus({
      city: location.label,
      icon: mapped.icon,
      temp: current.temperature,
      cond: mapped.label,
      humidity,
      wind,
      aqi: "--"
    });

    const forecastContainer = document.getElementById("forecast");
    if (forecastContainer && weather.daily && weather.daily.time) {
      forecastContainer.innerHTML = '';
      const daily = weather.daily;
      const maxDays = Math.min(4, daily.time.length - 1);
      for (let i = 1; i <= maxDays; i++) {
        const date = new Date(daily.time[i]);
        let dayName = new Intl.DateTimeFormat('zh-CN', { weekday: 'short' }).format(date);
        if (i === 1) dayName = '明天';
        
        const maxTemp = Math.round(daily.temperature_2m_max[i]);
        const minTemp = Math.round(daily.temperature_2m_min[i]);
        const forecastMapped = mapWeatherCode(daily.weathercode[i]);
        
        const div = document.createElement("div");
        div.className = "forecast-item";
        div.innerHTML = `
          <span class="forecast-day">${dayName}</span>
          <span class="forecast-icon">${forecastMapped.icon}</span>
          <span class="forecast-temp">${maxTemp}°<span class="forecast-temp-min">${minTemp}°</span></span>
        `;
        forecastContainer.appendChild(div);
      }
    }
  } catch (error) {
    console.warn(error);
    setError(error.message || "更新失败");
  }
}

function bindLocationSetting() {
  const savedName = getSavedLocationName();
  if (savedName) {
    elements.locationInput.value = savedName;
  }

  const handleSetLocation = async () => {
    const value = elements.locationInput.value.trim();
    if (!value) return;
    saveLocationName(value);
    
    elements.locationInput.blur();
    elements.locationInput.value = "加载中...";
    await updateWeather(value);
  };

  elements.locationInput.addEventListener("focus", () => {
    elements.locationInput.select();
  });

  elements.locationInput.addEventListener("keydown", async (event) => {
    if (event.isComposing || event.keyCode === 229) {
      return; // Ignore keydown during IME composition
    }
    if (event.key === "Enter") {
      event.preventDefault();
      await handleSetLocation();
    } else if (event.key === "Escape") {
      elements.locationInput.blur();
      // Restore previous value
      const saved = getSavedLocationName();
      elements.locationInput.value = saved || "未知城市";
    }
  });
}

bindLocationSetting();
updateWeather();
setInterval(() => updateWeather(), 10 * 60 * 1000);

// Theme Toggle Logic
function bindThemeToggle() {
  const themeToggle = document.getElementById("themeToggle");
  const currentTheme = localStorage.getItem("weatherTheme") || "dark";

  if (currentTheme === "light") {
    document.body.classList.add("light-theme");
    themeToggle.textContent = "🌙";
  } else {
    themeToggle.textContent = "☀️";
  }

  themeToggle.addEventListener("click", () => {
    document.body.classList.toggle("light-theme");
    const isLight = document.body.classList.contains("light-theme");
    localStorage.setItem("weatherTheme", isLight ? "light" : "dark");
    themeToggle.textContent = isLight ? "🌙" : "☀️";
  });
}

bindThemeToggle();

// Resize Logic
function bindResize() {
  const grip = document.getElementById("resizeGrip");
  if (!grip) return;

  let startX, startY, startWidth, startHeight;

  grip.addEventListener("mousedown", (e) => {
    e.preventDefault();
    startX = e.clientX;
    startY = e.clientY;
    startWidth = window.innerWidth;
    startHeight = window.innerHeight;

    const onMouseMove = (e) => {
      if (e.buttons === 0) {
        onMouseUp();
        return;
      }
      const deltaX = e.clientX - startX;
      const deltaY = e.clientY - startY;
      const newWidth = startWidth + deltaX;
      const newHeight = startHeight + deltaY;

      if (window.desktop && window.desktop.resizeWidget) {
        window.desktop.resizeWidget({ width: newWidth, height: newHeight });
      }
    };

    const onMouseUp = () => {
      document.removeEventListener("mousemove", onMouseMove);
      document.removeEventListener("mouseup", onMouseUp);
    };

    document.addEventListener("mousemove", onMouseMove);
    document.addEventListener("mouseup", onMouseUp);
  });
}

bindResize();
