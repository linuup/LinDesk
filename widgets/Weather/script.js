// ──────────────────────────────────────────────
// LinDesk Weather Widget - Runtime Logic & Responsive Engine
// ──────────────────────────────────────────────

const urlParams = new URLSearchParams(window.location.search);
const TARGET_WIDGET_ID = urlParams.get("widgetId") || "lindesk.weather";

const STORAGE_KEY_CITIES        = "lindesk_weather_cities";
const STORAGE_KEY_CURRENT_ID    = "lindesk_weather_current_city_id";
const STORAGE_KEY_THEME         = "lindesk_weather_theme";
const STORAGE_KEY_CACHE_PREFIX  = "lindesk_weather_cache_";
const STORAGE_KEY_LEGACY_LOC    = "lindesk_weather_location";

const REFRESH_INTERVAL   = 60 * 60 * 1000; // 1 hour
const CACHE_TTL          = 30 * 60 * 1000; // 30 minutes
const MAX_RETRIES        = 3;
const RETRY_DELAY        = 2000;
const MAX_RETRY_DELAY    = 30000;

let activeWeatherRequestId = 0;

// ──────────────────────────────────────────────
// Meteocons SVG Icon Mapping
// ──────────────────────────────────────────────
function getMeteoconSvg(name) {
  return `<img src="icons/${name}.svg" alt="${name}" class="weather-svg-icon" />`;
}

function mapWeatherCode(code, isNight = false) {
  const c = Number(code);
  if (c === 0) {
    return {
      label: "晴",
      iconSvg: getMeteoconSvg(isNight ? "clear-night" : "clear-day")
    };
  }
  if (c === 1 || c === 2) {
    return {
      label: "多云",
      iconSvg: getMeteoconSvg(isNight ? "partly-cloudy-night" : "partly-cloudy-day")
    };
  }
  if (c === 3) {
    return { label: "阴", iconSvg: getMeteoconSvg("overcast") };
  }
  if (c === 45 || c === 48) {
    return {
      label: "雾",
      iconSvg: getMeteoconSvg(isNight ? "fog-night" : "fog-day")
    };
  }
  if (c >= 51 && c <= 55) {
    return { label: "毛毛雨", iconSvg: getMeteoconSvg("drizzle") };
  }
  if (c === 61 || c === 63) {
    return {
      label: "小雨",
      iconSvg: getMeteoconSvg(isNight ? "partly-cloudy-night-rain" : "partly-cloudy-day-rain")
    };
  }
  if (c === 65) {
    return { label: "大雨", iconSvg: getMeteoconSvg("rain") };
  }
  if (c === 66 || c === 67) {
    return { label: "冻雨", iconSvg: getMeteoconSvg("sleet") };
  }
  if (c >= 71 && c <= 75) {
    return { label: "雪", iconSvg: getMeteoconSvg("snow") };
  }
  if (c === 77) {
    return { label: "雪粒", iconSvg: getMeteoconSvg("snowflake") };
  }
  if (c >= 80 && c <= 82) {
    return { label: "阵雨", iconSvg: getMeteoconSvg("rain") };
  }
  if (c === 85 || c === 86) {
    return { label: "阵雪", iconSvg: getMeteoconSvg("snow") };
  }
  if (c === 95) {
    return { label: "雷阵雨", iconSvg: getMeteoconSvg("thunderstorms") };
  }
  if (c === 96 || c === 99) {
    return { label: "强雷雨", iconSvg: getMeteoconSvg("thunderstorms-rain") };
  }
  return {
    label: "多云",
    iconSvg: getMeteoconSvg(isNight ? "partly-cloudy-night" : "partly-cloudy-day")
  };
}

function mapWttrCodeToWmo(codeStr) {
  const code = Number(codeStr);
  switch (code) {
    case 113: return 0;
    case 116: return 2;
    case 119:
    case 122: return 3;
    case 143:
    case 248:
    case 260: return 45;
    case 176:
    case 293:
    case 296: return 61;
    case 302:
    case 308: return 65;
    case 200:
    case 386:
    case 389: return 95;
    case 227:
    case 230:
    case 326:
    case 332: return 71;
    default: return 2;
  }
}

// ──────────────────────────────────────────────
// DOM Elements Cache
// ──────────────────────────────────────────────
const el = {
  locationBtn: document.getElementById("locationBtn"),
  locationCityName: document.getElementById("locationCityName"),
  locationSubtitle: document.getElementById("locationSubtitle"),
  settingsBtn: document.getElementById("settingsBtn"),
  
  emptyStateCard: document.getElementById("emptyStateCard"),
  emptyActionBtn: document.getElementById("emptyActionBtn"),
  weatherContent: document.getElementById("weatherContent"),
  
  icon: document.getElementById("icon"),
  temperature: document.getElementById("temperature"),
  condition: document.getElementById("condition"),
  tempRange: document.getElementById("tempRange"),
  feelsLike: document.getElementById("feelsLike"),
  miniPill: document.getElementById("miniPill"),
  
  aqi: document.getElementById("aqi"),
  humidity: document.getElementById("humidity"),
  wind: document.getElementById("wind"),
  pressure: document.getElementById("pressure"),
  
  forecast: document.getElementById("forecast")
};

// ──────────────────────────────────────────────
// Persistence & State Accessors
// ──────────────────────────────────────────────
const DEFAULT_FALLBACK_CITY = {
  id: "city_101200101",
  name: "武汉",
  city: "武汉市",
  prov: "湖北省",
  lat: 30.5928,
  lon: 114.3055,
  isCityLevel: true,
  display: "武汉市 · 湖北省"
};

function getSavedCities() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_CITIES);
    if (raw) {
      const arr = JSON.parse(raw);
      if (Array.isArray(arr) && arr.length > 0) return arr;
    }
  } catch (_) {}
  try {
    const legacy = localStorage.getItem(STORAGE_KEY_LEGACY_LOC);
    if (legacy) {
      const obj = JSON.parse(legacy);
      if (obj && obj.name) {
        const arr = [obj];
        try {
          localStorage.setItem(STORAGE_KEY_CITIES, JSON.stringify(arr));
        } catch (_) {}
        return arr;
      }
    }
  } catch (_) {}
  const fallback = [DEFAULT_FALLBACK_CITY];
  try {
    localStorage.setItem(STORAGE_KEY_CITIES, JSON.stringify(fallback));
    localStorage.setItem(STORAGE_KEY_CURRENT_ID, DEFAULT_FALLBACK_CITY.id);
  } catch (_) {}
  return fallback;
}

function getCurrentCityId() {
  return localStorage.getItem(STORAGE_KEY_CURRENT_ID) || DEFAULT_FALLBACK_CITY.id;
}

function setCurrentCityId(id) {
  localStorage.setItem(STORAGE_KEY_CURRENT_ID, id);
}

function getCurrentCity() {
  const cities = getSavedCities();
  if (!cities.length) return DEFAULT_FALLBACK_CITY;
  const currentId = getCurrentCityId();
  const found = cities.find(c => c.id === currentId);
  return found || cities[0];
}

function formatLocationDisplay(item) {
  if (!item) return "";
  const name = item.name || "";
  const city = item.city || "";
  const prov = item.prov || item.province || "";
  if (item.isCityLevel || name === city || name === city.replace(/(市|地区|盟|自治州)$/, "")) {
    if (!prov || prov === city) {
      return city;
    }
    return `${city} · ${prov}`;
  }
  if (!prov || prov === city) {
    return `${name} · ${city}`;
  }
  return `${name} · ${city} · ${prov}`;
}

// ──────────────────────────────────────────────
// Open Component Settings Page
// ──────────────────────────────────────────────
function openSettings() {
  const reqs = [
    { type: "open_widget_settings", widgetId: TARGET_WIDGET_ID },
    { type: "open_widget_settings", widgetId: "Weather" },
    { type: "widget_manager_request", action: "open_settings", widgetId: TARGET_WIDGET_ID },
    { type: "widget_manager_request", action: "open_settings", widgetId: "Weather" },
    { type: "linwidget_open_settings_request", widgetId: TARGET_WIDGET_ID }
  ];

  let dispatched = false;

  reqs.forEach((msg) => {
    if (window.linwidget && typeof window.linwidget.postMessage === "function") {
      try { window.linwidget.postMessage(msg); dispatched = true; } catch (_) {}
    }
    if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === "function") {
      try { window.chrome.webview.postMessage(msg); dispatched = true; } catch (_) {}
    }
    try {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "linwidget://bridge/postMessage", true);
      xhr.setRequestHeader("Content-Type", "application/json");
      xhr.send(JSON.stringify(msg));
      dispatched = true;
    } catch (_) {}
  });

  if (!dispatched) {
    window.open("settings.html", "_blank", "width=680,height=600");
  }
}

// ──────────────────────────────────────────────
// Cache Helpers
// ──────────────────────────────────────────────
function getCityCache(cityId) {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_CACHE_PREFIX + cityId);
    if (raw) return JSON.parse(raw);
  } catch (_) {}
  return null;
}

function saveCityCache(cityId, weatherData) {
  try {
    localStorage.setItem(STORAGE_KEY_CACHE_PREFIX + cityId, JSON.stringify({
      timestamp: Date.now(),
      data: weatherData
    }));
  } catch (_) {}
}

// ──────────────────────────────────────────────
// Networking
// ──────────────────────────────────────────────
function fetchJsonWithTimeout(url, timeoutMs = 8000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error("请求超时: " + url));
    }, timeoutMs);

    fetch(url)
      .then(res => {
        clearTimeout(timer);
        if (!res.ok) throw new Error("HTTP 状态异常: " + res.status);
        return res.json();
      })
      .then(resolve)
      .catch(err => {
        clearTimeout(timer);
        reject(err);
      });
  });
}

async function fetchWeatherOnce(latitude, longitude) {
  const openMeteoUrl = `https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}&current_weather=true&hourly=relativehumidity_2m,apparent_temperature,surface_pressure&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto`;
  try {
    const data = await fetchJsonWithTimeout(openMeteoUrl, 8000);
    if (data && data.current_weather) {
      return data;
    }
    throw new Error("Open-Meteo 数据不完整");
  } catch (primaryError) {
    console.warn("[Weather] Open-Meteo 备用转移至 wttr.in:", primaryError.message || primaryError);
    const wttrUrl = `https://wttr.in/${Number(latitude).toFixed(4)},${Number(longitude).toFixed(4)}?format=j1`;
    const wttrData = await fetchJsonWithTimeout(wttrUrl, 8000);
    const cc = wttrData?.current_condition?.[0];
    if (!cc) throw primaryError;
    const days = Array.isArray(wttrData.weather) ? wttrData.weather : [];
    return {
      current_weather: {
        temperature: Number(cc.temp_C),
        windspeed: Number(cc.windspeedKmph || 0),
        weathercode: mapWttrCodeToWmo(cc.weatherCode)
      },
      hourly: {
        relativehumidity_2m: [Number(cc.humidity || 0)],
        apparent_temperature: [Number(cc.FeelsLikeC || cc.temp_C)],
        surface_pressure: [Number(cc.pressure || 1013)]
      },
      daily: {
        time: days.map(d => d.date),
        temperature_2m_max: days.map(d => Number(d.maxtempC)),
        temperature_2m_min: days.map(d => Number(d.mintempC)),
        weathercode: days.map(d => mapWttrCodeToWmo(d.hourly?.[4]?.weatherCode || cc.weatherCode))
      }
    };
  }
}

async function fetchWeatherWithRetry(latitude, longitude, infiniteRetry = false) {
  let attempt = 0;
  const limit = infiniteRetry ? Infinity : MAX_RETRIES;
  while (attempt < limit) {
    attempt++;
    try {
      return await fetchWeatherOnce(latitude, longitude);
    } catch (error) {
      if (infiniteRetry) {
        const waitMs = Math.min(RETRY_DELAY * Math.pow(2, attempt - 1), MAX_RETRY_DELAY);
        await new Promise(r => setTimeout(r, waitMs));
      } else {
        if (attempt < MAX_RETRIES) {
          await new Promise(r => setTimeout(r, RETRY_DELAY));
        } else {
          throw error;
        }
      }
    }
  }
}

// ──────────────────────────────────────────────
// Render Weather Data
// ──────────────────────────────────────────────
function renderWeatherData(city, weather) {
  if (!weather || !weather.current_weather) return;
  const current = weather.current_weather;
  const currentHour = new Date().getHours();
  const isNight = currentHour < 6 || currentHour >= 18;
  const mapped = mapWeatherCode(current.weathercode, isNight);
  
  let matchedIdx = -1;
  let humidityValue = weather.hourly?.relativehumidity_2m?.[0];
  let feelsLikeVal = Math.round(current.temperature);
  let pressureVal = 1013;

  if (Array.isArray(weather.hourly?.time)) {
    const currentHourPrefix = typeof current.time === "string" ? current.time.slice(0, 13) : "";
    matchedIdx = currentHourPrefix
      ? weather.hourly.time.findIndex(t => typeof t === "string" && t.startsWith(currentHourPrefix))
      : -1;
    
    if (matchedIdx >= 0) {
      if (weather.hourly.relativehumidity_2m?.[matchedIdx] != null) {
        humidityValue = weather.hourly.relativehumidity_2m[matchedIdx];
      }
      if (weather.hourly.apparent_temperature?.[matchedIdx] != null) {
        feelsLikeVal = Math.round(weather.hourly.apparent_temperature[matchedIdx]);
      }
      if (weather.hourly.surface_pressure?.[matchedIdx] != null) {
        pressureVal = Math.round(weather.hourly.surface_pressure[matchedIdx]);
      }
    }
  }

  const humidity = humidityValue != null ? `${Math.round(humidityValue)}%` : "--%";
  const wind = `${Math.round(current.windspeed)} km/h`;

  const todayMax = Array.isArray(weather.daily?.temperature_2m_max) && weather.daily.temperature_2m_max[0] != null
    ? Math.round(weather.daily.temperature_2m_max[0])
    : null;
  const todayMin = Array.isArray(weather.daily?.temperature_2m_min) && weather.daily.temperature_2m_min[0] != null
    ? Math.round(weather.daily.temperature_2m_min[0])
    : null;

  // 基础气象信息渲染
  el.icon.innerHTML = mapped.iconSvg;
  el.temperature.textContent = `${Math.round(current.temperature)}°`;
  el.condition.textContent = mapped.label;
  el.humidity.textContent = humidity;
  el.wind.textContent = wind;
  el.aqi.textContent = "优";
  
  if (el.pressure) el.pressure.textContent = `${pressureVal} hPa`;
  if (el.feelsLike) el.feelsLike.textContent = `体感 ${feelsLikeVal}°`;
  
  if (el.tempRange) {
    el.tempRange.textContent = (todayMax != null && todayMin != null) ? `${todayMin}° / ${todayMax}°` : "";
  }

  // 小方形形态专用底部药丸胶囊（仅展示温差范围）
  if (el.miniPill) {
    el.miniPill.textContent = (todayMax != null && todayMin != null)
      ? `${todayMin}° / ${todayMax}°`
      : "";
  }

  // 多日天气预报列表渲染
  if (el.forecast && weather.daily && weather.daily.time) {
    el.forecast.innerHTML = "";
    const daily = weather.daily;
    const maxDays = Math.min(4, daily.time.length - 1);
    for (let i = 1; i <= maxDays; i++) {
      const date = new Date(daily.time[i]);
      let dayName = new Intl.DateTimeFormat("zh-CN", { weekday: "short" }).format(date);
      if (i === 1) dayName = "明天";

      const maxTemp = Math.round(daily.temperature_2m_max[i]);
      const minTemp = Math.round(daily.temperature_2m_min[i]);
      const forecastMapped = mapWeatherCode(daily.weathercode[i]);

      const itemEl = document.createElement("div");
      itemEl.className = "forecast-item";
      itemEl.innerHTML = `
        <span class="forecast-day">${dayName}</span>
        <span class="forecast-icon">${forecastMapped.iconSvg}</span>
        <span class="forecast-temp">${maxTemp}°<span class="forecast-temp-min">${minTemp}°</span></span>
      `;
      el.forecast.appendChild(itemEl);
    }
  }
}

// ──────────────────────────────────────────────
// State Updater Pipeline
// ──────────────────────────────────────────────
async function updateActiveWeather(forceRetry = false) {
  const currentCity = getCurrentCity();
  if (!currentCity) {
    showEmptyState();
    return;
  }

  showNormalWeather(currentCity);

  const cached = getCityCache(currentCity.id);
  if (cached && cached.data && !forceRetry) {
    renderWeatherData(currentCity, cached.data);
  } else {
    el.condition.textContent = `正在同步 ${currentCity.name} 气象...`;
    if (el.tempLarge && (!cached || !cached.data)) {
      el.tempLarge.style.opacity = "0.5";
    }
  }

  const requestId = ++activeWeatherRequestId;

  try {
    const data = await fetchWeatherWithRetry(currentCity.lat, currentCity.lon, forceRetry);
    if (requestId !== activeWeatherRequestId) return;
    if (el.tempLarge) {
      el.tempLarge.style.opacity = "1";
    }
    renderWeatherData(currentCity, data);
    saveCityCache(currentCity.id, data);
  } catch (err) {
    if (requestId !== activeWeatherRequestId) return;
    console.warn("[Weather] 更新天气失败:", err.message || err);
    if (el.tempLarge) {
      el.tempLarge.style.opacity = "1";
    }
    if (!cached) {
      el.condition.textContent = "获取天气超时，点击重试";
    }
  }
}

function showEmptyState() {
  el.emptyStateCard.classList.remove("hidden");
  el.weatherContent.classList.add("hidden");
  el.locationCityName.textContent = "未配置";
  el.locationBtn.title = "尚未配置城市，点击打开设置";
}

function showNormalWeather(currentCity) {
  el.emptyStateCard.classList.add("hidden");
  el.weatherContent.classList.remove("hidden");
  el.locationCityName.textContent = currentCity.name;
  el.locationBtn.title = `当前展示: ${currentCity.display || formatLocationDisplay(currentCity)} 点击打开城市设置`;
}

// ──────────────────────────────────────────────
// 智能自适应三种 UI 形态状态机
// 1. layout-small-square : 小方形（紧凑精致，大字大图标）
// 2. layout-rectangle    : 横向矩形（双栏平衡看板）
// 3. layout-large-square : 大方形（四宫格仪表盘 + 多日横幅）
// ──────────────────────────────────────────────
function evaluateLayout(width, height) {
  const w = width || window.innerWidth;
  const h = height || window.innerHeight;
  const ratio = w / (h || 1);

  document.body.classList.remove("layout-small-square", "layout-rectangle", "layout-large-square");

  if (w <= 220 || h <= 210 || (w <= 240 && h <= 240 && ratio >= 0.75 && ratio <= 1.35)) {
    document.body.classList.add("layout-small-square");
  } else if (w >= 250 && h >= 240) {
    document.body.classList.add("layout-large-square");
  } else {
    document.body.classList.add("layout-rectangle");
  }
}

function setupResponsiveObserver() {
  evaluateLayout(window.innerWidth, window.innerHeight);

  if (window.ResizeObserver) {
    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const { width, height } = entry.contentRect;
        evaluateLayout(width, height);
      }
    });
    observer.observe(document.documentElement);
  } else {
    window.addEventListener("resize", () => {
      evaluateLayout(window.innerWidth, window.innerHeight);
    });
  }
}

// ──────────────────────────────────────────────
// IPC & Event Listeners
// ──────────────────────────────────────────────
function applyWidgetSettings(settings) {
  if (!settings || typeof settings !== "object") return;

  let changed = false;

  if (Array.isArray(settings.cities) && settings.cities.length > 0) {
    try {
      localStorage.setItem(STORAGE_KEY_CITIES, JSON.stringify(settings.cities));
      changed = true;
    } catch (_) {}
  }

  if (typeof settings.currentCityId === "string" && settings.currentCityId) {
    try {
      localStorage.setItem(STORAGE_KEY_CURRENT_ID, settings.currentCityId);
      changed = true;
    } catch (_) {}
  } else if (Array.isArray(settings.cities) && settings.cities.length > 0) {
    const curId = getCurrentCityId();
    if (!curId || !settings.cities.some(c => c.id === curId)) {
      try {
        localStorage.setItem(STORAGE_KEY_CURRENT_ID, settings.cities[0].id);
        changed = true;
      } catch (_) {}
    }
  }

  // 立即在界面上反馈切换后的城市名，并平滑显示过渡状态
  const targetCity = getCurrentCity();
  if (targetCity) {
    showNormalWeather(targetCity);
    if (el.condition) {
      el.condition.textContent = `正在同步 ${targetCity.name} 天气...`;
    }
  }

  updateActiveWeather(true);
}

function bindEvents() {
  el.locationBtn.addEventListener("click", openSettings);
  el.settingsBtn.addEventListener("click", openSettings);
  el.emptyActionBtn.addEventListener("click", openSettings);

  let hoverTimer = null;
  const glassPanel = document.getElementById("glassPanel");

  function handleMouseEnter() {
    if (hoverTimer) {
      clearTimeout(hoverTimer);
      hoverTimer = null;
    }
    document.body.classList.add("is-hovered");
  }

  function handleMouseLeave() {
    if (hoverTimer) clearTimeout(hoverTimer);
    hoverTimer = setTimeout(() => {
      document.body.classList.remove("is-hovered");
    }, 150);
  }

  window.addEventListener("mouseenter", handleMouseEnter);
  window.addEventListener("mouseleave", handleMouseLeave);
  document.addEventListener("mouseenter", handleMouseEnter);
  document.addEventListener("mouseleave", handleMouseLeave);
  if (glassPanel) {
    glassPanel.addEventListener("mouseenter", handleMouseEnter);
    glassPanel.addEventListener("mouseleave", handleMouseLeave);
  }

  window.addEventListener("mousemove", () => {
    handleMouseEnter();
    if (hoverTimer) clearTimeout(hoverTimer);
    hoverTimer = setTimeout(() => {
      document.body.classList.remove("is-hovered");
    }, 1200);
  });

  window.addEventListener("storage", (event) => {
    if (!event.key) return;
    if (event.key === STORAGE_KEY_CITIES || event.key === STORAGE_KEY_CURRENT_ID) {
      const city = getCurrentCity();
      if (city) {
        showNormalWeather(city);
        if (el.condition) {
          el.condition.textContent = `正在同步 ${city.name} 天气...`;
        }
      }
      updateActiveWeather(true);
    }
  });

  const handleMessage = (event) => {
    const data = event && event.data;
    if (!data || typeof data !== "object") return;

    if (data.type === "permission_changed") {
      if (data.permission === "network") {
        if (data.granted) {
          updateActiveWeather(true);
        } else {
          const el = document.getElementById("forecast-temp");
          if (el) el.title = "网络权限已关闭，已暂停更新天气";
        }
      }
      return;
    }

    if (data.type === "linwidget_settings_changed_notification" || 
        data.type === "settings_changed" || 
        data.type === "settings_updated" ||
        data.type === "linwidget_settings_sync" ||
        data.type === "linwidget_appearance_changed_notification" ||
        data.type === "linwidget_settings_changed") {
      applyWidgetSettings(data.settings || data);
      return;
    }

    if (data.action === "save_widget_settings" && data.settings) {
      applyWidgetSettings(data.settings);
      return;
    }

    if (data.action === "info") {
      if (data.widget && data.widget.settingsValues) {
        applyWidgetSettings(data.widget.settingsValues);
      } else if (data.settingsValues) {
        applyWidgetSettings(data.settingsValues);
      }
      return;
    }
  };

  window.addEventListener("message", handleMessage);
  if (window.chrome && window.chrome.webview && typeof window.chrome.webview.addEventListener === "function") {
    window.chrome.webview.addEventListener("message", handleMessage);
  }
}

function queryHostSettings() {
  const reqs = [
    { type: "widget_manager_request", action: "info", widgetId: "lindesk.weather" },
    { type: "widget_manager_request", action: "info", widgetId: "Weather" }
  ];
  reqs.forEach((req) => {
    if (window.linwidget && typeof window.linwidget.postMessage === "function") {
      try { window.linwidget.postMessage(req); } catch (_) {}
    }
    if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === "function") {
      try { window.chrome.webview.postMessage(req); } catch (_) {}
    }
    try {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "linwidget://bridge/postMessage", true);
      xhr.setRequestHeader("Content-Type", "application/json");
      xhr.send(JSON.stringify(req));
    } catch (_) {}
  });
}

// ──────────────────────────────────────────────
// Boot Sequence
// ──────────────────────────────────────────────
setupResponsiveObserver();
bindEvents();
queryHostSettings();

const initialCities = getSavedCities();
if (initialCities.length > 0) {
  if (!getCurrentCityId()) {
    setCurrentCityId(initialCities[0].id);
  }
  updateActiveWeather(false);
} else {
  showEmptyState();
}

setInterval(() => updateActiveWeather(false), REFRESH_INTERVAL);
