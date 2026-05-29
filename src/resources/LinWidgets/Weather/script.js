// ──────────────────────────────────────────────
// Storage keys (namespaced to avoid collisions)
// ──────────────────────────────────────────────
const STORAGE_KEY_THEME    = "lindesk_weather_theme";
const STORAGE_KEY_LOCATION = "lindesk_weather_location"; // JSON: { latitude, longitude, label, query }
const STORAGE_KEY_CACHE    = "lindesk_weather_cache";    // JSON: { timestamp, locationLabel, data }

const REFRESH_INTERVAL = 60 * 60 * 1000; // 1 hour
const MAX_RETRIES      = 3;
const RETRY_DELAY      = 3000; // 3 seconds between retries

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

// ──────────────────────────────────────────────
// Persistence helpers
// ──────────────────────────────────────────────

/** Read the persisted location object (with lat/lon/label/query). */
function getSavedLocation() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_LOCATION);
    if (raw) {
      const obj = JSON.parse(raw);
      if (obj && typeof obj.latitude === "number" && typeof obj.longitude === "number"
          && (obj.latitude !== 0 || obj.longitude !== 0)) {
        return obj;
      }
    }
  } catch (_) { /* corrupted data – ignore */ }
  return null;
}

/** Return just the user-typed query string, or "". */
function getSavedLocationQuery() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_LOCATION);
    if (raw) {
      const obj = JSON.parse(raw);
      return obj?.query || obj?.label || "";
    }
  } catch (_) { /* ignore */ }
  return "";
}

/** Persist the full geocoded location + the original query. */
function saveLocation(latitude, longitude, label, query) {
  localStorage.setItem(
    STORAGE_KEY_LOCATION,
    JSON.stringify({ latitude, longitude, label, query: query || label })
  );
}

// ──────────────────────────────────────────────
// UI helpers
// ──────────────────────────────────────────────

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

// ──────────────────────────────────────────────
// Geocoding
// ──────────────────────────────────────────────

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

/**
 * Resolve the location to use for weather queries.
 * Priority: 1) cached location (instant, no network)  2) IP geolocation  3) default
 */
async function getLocation() {
  // 1. Use the cached full location (lat/lon) directly – no geocode needed
  const cached = getSavedLocation();
  if (cached) {
    return cached;
  }

  // 2. If a query string is saved but coords are missing (e.g. legacy migration), geocode it
  const savedQuery = getSavedLocationQuery();
  if (savedQuery) {
    const geocoded = await geocodeLocation(savedQuery);
    if (geocoded) {
      // Back-fill the cache so the next launch skips geocoding
      saveLocation(geocoded.latitude, geocoded.longitude, geocoded.label, savedQuery);
      return geocoded;
    }
  }

  // 3. IP-based geolocation fallback
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

// ──────────────────────────────────────────────
// Weather data cache
// ──────────────────────────────────────────────

function getSavedWeatherCache() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY_CACHE);
    if (raw) return JSON.parse(raw);
  } catch (_) { /* corrupted – ignore */ }
  return null;
}

function saveWeatherCache(locationLabel, weatherData) {
  localStorage.setItem(STORAGE_KEY_CACHE, JSON.stringify({
    timestamp: Date.now(),
    locationLabel,
    data: weatherData
  }));
}

/** Render weather data (from API response or cache) onto the UI. */
function renderWeatherData(locationLabel, weather) {
  const current = weather.current_weather;
  if (!current) return;

  const mapped = mapWeatherCode(current.weathercode);
  const humidity = weather.hourly?.relativehumidity_2m?.[0] != null
    ? `${Math.round(weather.hourly.relativehumidity_2m[0])}%`
    : "--%";
  const wind = `${Math.round(current.windspeed)} km/h`;

  setStatus({
    city: locationLabel,
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
}

// ──────────────────────────────────────────────
// Weather fetching (with retry)
// ──────────────────────────────────────────────

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * Fetch weather data with retry logic.
 * Attempts up to MAX_RETRIES times with RETRY_DELAY between attempts.
 * Returns the parsed JSON on success, or throws on complete failure.
 */
async function fetchWeatherWithRetry(latitude, longitude) {
  const url = `https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}&current_weather=true&hourly=relativehumidity_2m&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto`;

  let lastError;
  for (let attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 10000); // 10s timeout
      const response = await fetch(url, { signal: controller.signal });
      clearTimeout(timeoutId);
      if (!response.ok) {
        throw new Error(`天气服务返回 ${response.status}`);
      }
      const data = await response.json();
      if (!data.current_weather) {
        throw new Error("当前天气数据缺失");
      }
      console.log(`[Weather] 第 ${attempt} 次请求成功`);
      return data;
    } catch (error) {
      lastError = error;
      console.warn(`[Weather] 第 ${attempt}/${MAX_RETRIES} 次请求失败:`, error.message);
      if (attempt < MAX_RETRIES) {
        await delay(RETRY_DELAY);
      }
    }
  }
  throw lastError;
}

async function updateWeather(locationQuery) {
  try {
    let location;
    if (locationQuery) {
      location = await geocodeLocation(locationQuery);
      // Persist the full geocoded result so the next launch is instant
      if (location) {
        saveLocation(location.latitude, location.longitude, location.label, locationQuery);
      }
    }
    location = location || await getLocation();

    const weather = await fetchWeatherWithRetry(location.latitude, location.longitude);

    // Success – render and update cache
    renderWeatherData(location.label, weather);
    saveWeatherCache(location.label, weather);
    console.log(`[Weather] 数据已更新并缓存`);
  } catch (error) {
    console.warn(`[Weather] 全部 ${MAX_RETRIES} 次请求均失败，尝试使用缓存`, error.message);

    // Fall back to cached data
    const cache = getSavedWeatherCache();
    if (cache && cache.data) {
      const ageMinutes = Math.round((Date.now() - cache.timestamp) / 60000);
      console.log(`[Weather] 使用 ${ageMinutes} 分钟前的缓存数据`);
      renderWeatherData(cache.locationLabel, cache.data);
    } else {
      // No cache at all – show error
      setError("无法获取天气（无缓存）");
    }
  }
}

// ──────────────────────────────────────────────
// Location input binding
// ──────────────────────────────────────────────

function bindLocationSetting() {
  const savedQuery = getSavedLocationQuery();
  if (savedQuery) {
    elements.locationInput.value = savedQuery;
  }

  const handleSetLocation = async () => {
    const value = elements.locationInput.value.trim();
    if (!value) return;
    
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
      const saved = getSavedLocationQuery();
      elements.locationInput.value = saved || "未知城市";
    }
  });
}

// ──────────────────────────────────────────────
// Theme Toggle Logic
// ──────────────────────────────────────────────

function bindThemeToggle() {
  const themeToggle = document.getElementById("themeToggle");
  const currentTheme = localStorage.getItem(STORAGE_KEY_THEME) || "dark";

  if (currentTheme === "light") {
    document.body.classList.add("light-theme");
    themeToggle.textContent = "🌙";
  } else {
    themeToggle.textContent = "☀️";
  }

  themeToggle.addEventListener("click", () => {
    document.body.classList.toggle("light-theme");
    const isLight = document.body.classList.contains("light-theme");
    localStorage.setItem(STORAGE_KEY_THEME, isLight ? "light" : "dark");
    themeToggle.textContent = isLight ? "🌙" : "☀️";
  });
}

// ──────────────────────────────────────────────
// Migrate legacy keys (one-time, from old "weatherTheme" / "weatherLocation")
// ──────────────────────────────────────────────
function migrateLegacyKeys() {
  const oldTheme = localStorage.getItem("weatherTheme");
  if (oldTheme) {
    localStorage.setItem(STORAGE_KEY_THEME, oldTheme);
    localStorage.removeItem("weatherTheme");
  }
  const oldLoc = localStorage.getItem("weatherLocation");
  if (oldLoc) {
    // Old format was just a city name string — wrap it as a query-only object
    // so the next weather update will geocode and fill in lat/lon
    localStorage.setItem(STORAGE_KEY_LOCATION, JSON.stringify({ latitude: 0, longitude: 0, label: oldLoc, query: oldLoc, needsGeocode: true }));
    localStorage.removeItem("weatherLocation");
  }
}

// ──────────────────────────────────────────────
// Boot
// ──────────────────────────────────────────────
migrateLegacyKeys();
bindThemeToggle();
bindLocationSetting();

// Immediately render cached data (instant, no network), then fetch fresh data
const cachedWeather = getSavedWeatherCache();
if (cachedWeather && cachedWeather.data) {
  console.log("[Weather] 启动时使用缓存数据渲染");
  renderWeatherData(cachedWeather.locationLabel, cachedWeather.data);
}
// Fetch fresh data in background
updateWeather();
// Refresh every 1 hour
setInterval(() => updateWeather(), REFRESH_INTERVAL);
