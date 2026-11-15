// ──────────────────────────────────────────────
// LinDesk Weather - Core Desktop Widget Runtime
// ──────────────────────────────────────────────

const STORAGE_KEY_CITIES        = "lindesk_weather_cities";
const STORAGE_KEY_CURRENT_ID    = "lindesk_weather_current_city_id";
const STORAGE_KEY_THEME         = "lindesk_weather_theme";
const STORAGE_KEY_CACHE_PREFIX  = "lindesk_weather_cache_";
const STORAGE_KEY_LEGACY_LOC    = "lindesk_weather_location";

const REFRESH_INTERVAL   = 60 * 60 * 1000; // 1 hour
const MAX_RETRIES        = 3;
const RETRY_DELAY        = 3000;
const MAX_RETRY_DELAY    = 30000;

const weatherCodeMap = {
  0: { icon: "clear-day.svg", iconNight: "clear-night.svg", label: "晴朗" },
  1: { icon: "partly-cloudy-day.svg", iconNight: "partly-cloudy-night.svg", label: "晴间多云" },
  2: { icon: "partly-cloudy-day.svg", iconNight: "partly-cloudy-night.svg", label: "多云" },
  3: { icon: "overcast.svg", iconNight: "overcast.svg", label: "阴天" },
  45: { icon: "fog.svg", iconNight: "fog-night.svg", label: "大雾" },
  48: { icon: "fog.svg", iconNight: "fog-night.svg", label: "冻雾" },
  51: { icon: "drizzle.svg", iconNight: "drizzle.svg", label: "毛毛细雨" },
  53: { icon: "drizzle.svg", iconNight: "drizzle.svg", label: "中度细雨" },
  55: { icon: "drizzle.svg", iconNight: "drizzle.svg", label: "浓密细雨" },
  56: { icon: "sleet.svg", iconNight: "sleet.svg", label: "轻度冻雨" },
  57: { icon: "sleet.svg", iconNight: "sleet.svg", label: "密集冻雨" },
  61: { icon: "rain.svg", iconNight: "rain.svg", label: "小雨" },
  63: { icon: "rain.svg", iconNight: "rain.svg", label: "中雨" },
  65: { icon: "extreme-rain.svg", iconNight: "extreme-rain.svg", label: "大暴雨" },
  66: { icon: "sleet.svg", iconNight: "sleet.svg", label: "轻度冰雨" },
  67: { icon: "hail.svg", iconNight: "hail.svg", label: "强冰雹" },
  71: { icon: "snow.svg", iconNight: "snow.svg", label: "小雪" },
  73: { icon: "snow.svg", iconNight: "snow.svg", label: "中雪" },
  75: { icon: "snow.svg", iconNight: "snow.svg", label: "大雪" },
  77: { icon: "snow.svg", iconNight: "snow.svg", label: "雪粒" },
  80: { icon: "partly-cloudy-day-rain.svg", iconNight: "partly-cloudy-night-rain.svg", label: "阵雨" },
  81: { icon: "partly-cloudy-day-rain.svg", iconNight: "partly-cloudy-night-rain.svg", label: "强阵雨" },
  82: { icon: "extreme-day-rain.svg", iconNight: "extreme-rain.svg", label: "极端暴雨" },
  85: { icon: "partly-cloudy-day-snow.svg", iconNight: "partly-cloudy-night-snow.svg", label: "阵雪" },
  86: { icon: "partly-cloudy-day-snow.svg", iconNight: "partly-cloudy-night-snow.svg", label: "强阵雪" },
  95: { icon: "thunderstorms-day-rain.svg", iconNight: "thunderstorms-night-rain.svg", label: "雷阵雨" },
  96: { icon: "thunderstorms-day-rain.svg", iconNight: "thunderstorms-night-rain.svg", label: "雷阵雨伴有冰雹" },
  99: { icon: "thunderstorms.svg", iconNight: "thunderstorms.svg", label: "强雷暴" }
};

function getMeteoconsIconHtml(iconFile, alt = "Weather") {
  return `<img src="icons/${iconFile}" alt="${alt}" class="meteocons-svg" />`;
}

// ──────────────────────────────────────────────
// DOM Elements Cache
// ──────────────────────────────────────────────
const el = {
  locationBtn: document.getElementById("locationBtn"),
  locationCityName: document.getElementById("locationCityName"),
  cityDots: document.getElementById("cityDots"),
  settingsBtn: document.getElementById("settingsBtn"),
  
  emptyStateCard: document.getElementById("emptyStateCard"),
  emptyActionBtn: document.getElementById("emptyActionBtn"),
  weatherContent: document.getElementById("weatherContent"),
  
  icon: document.getElementById("icon"),
  temperature: document.getElementById("temperature"),
  condition: document.getElementById("condition"),
  aqi: document.getElementById("aqi"),
  humidity: document.getElementById("humidity"),
  wind: document.getElementById("wind"),
  forecast: document.getElementById("forecast")
};

// ──────────────────────────────────────────────
// Persistence & State Accessors
// ──────────────────────────────────────────────

function getSavedCities() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_CITIES);
    if (raw) {
      const arr = JSON.parse(raw);
      if (Array.isArray(arr)) return arr;
    }
  } catch (_) {}
  return [];
}

function getCurrentCityId() {
  return localStorage.getItem(STORAGE_KEY_CURRENT_ID) || "";
}

function setCurrentCityId(id) {
  localStorage.setItem(STORAGE_KEY_CURRENT_ID, id);
}

function getCurrentCity() {
  const cities = getSavedCities();
  if (!cities.length) return null;
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
  if (window.desktop && typeof desktop.openSettings === "function") {
    desktop.openSettings();
  } else {
    window.open("settings.html", "WeatherSettings", "width=680,height=640");
  }
}

// ──────────────────────────────────────────────
// Weather API & Cache
// ──────────────────────────────────────────────

function mapWeatherCode(code, isNight = false) {
  const item = weatherCodeMap[code];
  if (!item) {
    const fallbackIcon = isNight ? "clear-night.svg" : "clear-day.svg";
    return {
      iconFile: fallbackIcon,
      iconSvg: getMeteoconsIconHtml(fallbackIcon, "未知天气"),
      label: "未知天气"
    };
  }
  const iconFile = (isNight && item.iconNight) ? item.iconNight : item.icon;
  return {
    iconFile,
    iconSvg: getMeteoconsIconHtml(iconFile, item.label),
    label: item.label
  };
}

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

async function fetchJsonWithTimeout(url, timeoutMs = 7000) {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, { signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.json();
  } finally {
    clearTimeout(timeoutId);
  }
}

function mapWttrCodeToWmo(wttrCode) {
  const code = Number(wttrCode);
  if (code === 113) return 0;
  if (code === 116) return 2;
  if (code === 119 || code === 122) return 3;
  if (code === 143 || code === 248 || code === 260) return 45;
  if ([176, 263, 266, 293, 296, 353].includes(code)) return 61;
  if ([299, 302, 305, 308, 356, 359].includes(code)) return 65;
  if ([179, 182, 185, 227, 230, 323, 326, 329, 332, 335, 338, 368, 371].includes(code)) return 73;
  if ([200, 386, 389, 392, 395].includes(code)) return 95;
  return 2;
}

async function fetchWeatherOnce(latitude, longitude) {
  const openMeteoUrl = `https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}&current_weather=true&hourly=relativehumidity_2m&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto`;
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
        relativehumidity_2m: [Number(cc.humidity || 0)]
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

function renderWeatherData(city, weather) {
  if (!weather || !weather.current_weather) return;
  const current = weather.current_weather;
  const currentHour = new Date().getHours();
  const isNight = currentHour < 6 || currentHour >= 18;
  const mapped = mapWeatherCode(current.weathercode, isNight);
  
  let humidityValue = weather.hourly?.relativehumidity_2m?.[0];
  if (Array.isArray(weather.hourly?.time) && Array.isArray(weather.hourly?.relativehumidity_2m)) {
    const currentHourPrefix = typeof current.time === "string" ? current.time.slice(0, 13) : "";
    const matchedIdx = currentHourPrefix
      ? weather.hourly.time.findIndex(t => typeof t === "string" && t.startsWith(currentHourPrefix))
      : -1;
    if (matchedIdx >= 0 && weather.hourly.relativehumidity_2m[matchedIdx] != null) {
      humidityValue = weather.hourly.relativehumidity_2m[matchedIdx];
    }
  }
  const humidity = humidityValue != null ? `${Math.round(humidityValue)}%` : "--%";
  const wind = `${Math.round(current.windspeed)} km/h`;

  el.icon.innerHTML = mapped.iconSvg;
  el.temperature.textContent = `${Math.round(current.temperature)}°`;
  el.condition.textContent = mapped.label;
  el.humidity.textContent = humidity;
  el.wind.textContent = wind;
  el.aqi.textContent = "--";

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
// App State & UI Updater
// ──────────────────────────────────────────────

let activeWeatherRequestId = 0;

async function updateActiveWeather(forceRetry = false) {
  const currentCity = getCurrentCity();
  if (!currentCity) {
    showEmptyState();
    return;
  }

  showNormalWeather(currentCity);

  const cached = getCityCache(currentCity.id);
  if (cached && cached.data) {
    renderWeatherData(currentCity, cached.data);
  } else {
    el.condition.textContent = "同步气象中...";
  }

  const requestId = ++activeWeatherRequestId;

  try {
    const data = await fetchWeatherWithRetry(currentCity.lat, currentCity.lon, false);
    if (requestId !== activeWeatherRequestId) return;
    renderWeatherData(currentCity, data);
    saveCityCache(currentCity.id, data);
  } catch (err) {
    if (requestId !== activeWeatherRequestId) return;
    console.warn("[Weather] 更新天气失败:", err.message || err);
    if (!cached) {
      el.condition.textContent = "获取天气超时";
    }
  }
}

function showEmptyState() {
  el.emptyStateCard.classList.remove("hidden");
  el.weatherContent.classList.add("hidden");
  el.locationCityName.textContent = "未配置";
  el.locationBtn.title = "尚未配置城市，点击打开设置";
  if (el.cityDots) el.cityDots.innerHTML = "";
}

function showNormalWeather(currentCity) {
  el.emptyStateCard.classList.add("hidden");
  el.weatherContent.classList.remove("hidden");
  el.locationCityName.textContent = currentCity.name;
  el.locationBtn.title = `当前展示: ${currentCity.display || formatLocationDisplay(currentCity)} 点击打开城市设置`;
  renderCityDots();
}

function renderCityDots() {
  if (!el.cityDots) return;
  el.cityDots.innerHTML = "";
}

function switchCity(cityId) {
  setCurrentCityId(cityId);
  renderCityDots();
  updateActiveWeather(false);
}

// ──────────────────────────────────────────────
// Theme Management
// ──────────────────────────────────────────────

function initTheme() {
  const currentTheme = localStorage.getItem(STORAGE_KEY_THEME) || "dark";
  const isLight = currentTheme === "light";
  document.body.classList.toggle("light-theme", isLight);
}

// ──────────────────────────────────────────────
// Legacy Data Migration
// ──────────────────────────────────────────────

function migrateLegacyLocation() {
  const existing = getSavedCities();
  if (existing.length > 0) return;

  try {
    const raw = localStorage.getItem(STORAGE_KEY_LEGACY_LOC);
    if (raw) {
      const oldObj = JSON.parse(raw);
      if (oldObj && typeof oldObj.latitude === "number" && typeof oldObj.longitude === "number") {
        const id = "legacy_" + Math.round(oldObj.latitude * 100);
        const name = (oldObj.label || oldObj.query || "本地").split(/[,\s]/)[0];
        const newCity = {
          id,
          name,
          city: name,
          prov: "",
          lat: Number(oldObj.latitude),
          lon: Number(oldObj.longitude),
          display: oldObj.label || name
        };
        localStorage.setItem(STORAGE_KEY_CITIES, JSON.stringify([newCity]));
        setCurrentCityId(id);
      }
    }
  } catch (_) {}
}

// ──────────────────────────────────────────────
// Event Bindings
// ──────────────────────────────────────────────

function applyWidgetSettings(settings) {
  if (!settings || typeof settings !== "object") return;

  if (Array.isArray(settings.cities) && settings.cities.length > 0) {
    try {
      localStorage.setItem(STORAGE_KEY_CITIES, JSON.stringify(settings.cities));
    } catch (_) {}
  }

  if (typeof settings.currentCityId === "string" && settings.currentCityId) {
    try {
      localStorage.setItem(STORAGE_KEY_CURRENT_ID, settings.currentCityId);
    } catch (_) {}
  } else if (Array.isArray(settings.cities) && settings.cities.length > 0) {
    const curId = getCurrentCityId();
    if (!curId || !settings.cities.some(c => c.id === curId)) {
      try {
        localStorage.setItem(STORAGE_KEY_CURRENT_ID, settings.cities[0].id);
      } catch (_) {}
    }
  }

  if (typeof settings.theme === "string") {
    const isL = settings.theme === "light";
    document.body.classList.toggle("light-theme", isL);
    try {
      localStorage.setItem(STORAGE_KEY_THEME, settings.theme);
    } catch (_) {}
  }

  renderCityDots();
  updateActiveWeather(true);
}

function syncSettingsFromBackend() {
  try {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "linwidget://bridge/postMessage", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.send(JSON.stringify({
      type: "widget_manager_request",
      action: "info",
      widgetId: "lindesk.weather"
    }));
  } catch (_) {}
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

  // 鼠标移动活性检测：1.2秒内未收到移动事件判定鼠标离开
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
      renderCityDots();
      updateActiveWeather(true);
    } else if (event.key === STORAGE_KEY_THEME) {
      const isL = event.newValue === "light";
      document.body.classList.toggle("light-theme", isL);
    }
  });

  const handleMessage = (event) => {
    const data = event && event.data;
    if (!data || typeof data !== "object") return;

    if (data.type === "linwidget_settings_changed_notification" || data.type === "settings_changed" || data.type === "settings_updated") {
      applyWidgetSettings(data.settings || data);
      return;
    }

    if (data.action === "info" && data.widget && data.widget.settingsValues) {
      applyWidgetSettings(data.widget.settingsValues);
      return;
    }

    if (data.type === "linwidget_appearance_changed_notification") {
      const settings = data.settings;
      if (settings && typeof settings === "object" && typeof settings.theme === "string") {
        const isL = settings.theme === "light";
        document.body.classList.toggle("light-theme", isL);
        try {
          localStorage.setItem(STORAGE_KEY_THEME, settings.theme);
        } catch (_) {}
      }
    }
  };

  window.addEventListener("message", handleMessage);
  if (window.chrome && window.chrome.webview && typeof window.chrome.webview.addEventListener === "function") {
    window.chrome.webview.addEventListener("message", handleMessage);
  }
}

// ──────────────────────────────────────────────
// Boot Sequence
// ──────────────────────────────────────────────

migrateLegacyLocation();
initTheme();
bindEvents();
syncSettingsFromBackend();

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
