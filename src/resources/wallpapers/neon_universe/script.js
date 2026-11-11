// Breeze Meadow (清风原野) · LinDesk 治愈系纯代码动态自然壁纸
// 纯 Canvas 2D 纯代码手绘：晨曦天青、丁达尔光束、微风草浪、起舞蒲公英与灵动光斑

const canvas = document.getElementById('wallpaper-canvas');
const ctx = canvas.getContext('2d', { alpha: false });

// 基础视口与渲染状态
let width = canvas.width = window.innerWidth;
let height = canvas.height = window.innerHeight;
let dpr = Math.min(window.devicePixelRatio || 1, 2);

let isRunning = true;
let animationFrameId = null;
let lastTime = performance.now();
let globalTime = 0;

// 配置与属性 (支持 LinDesk 壁纸工坊实时调节)
const config = {
  windSpeed: 4,          // 微风速度 1~10
  dandelionCount: 35,    // 蒲公英飞絮密度 10~80
  sunbeamIntensity: 6,   // 晨光丁达尔强度 0~10
  showClock: true        // 是否显示环境时钟
};

// 交互微风与鼠标状态
const mouseWind = {
  x: -1000,
  y: -1000,
  vx: 0,
  vy: 0,
  active: false,
  radius: 220,
  force: 0,
  lastX: 0,
  lastY: 0,
  pulseTimer: 0
};

// 音频响应频谱
let audioFrequencies = new Array(32).fill(0);
let audioVolume = 0;

// ----------------------------------------------------
// 1. 蒲公英飞絮系统 (Dandelion Parachutes)
// ----------------------------------------------------
class DandelionSeed {
  constructor(initialSpread = false) {
    this.reset(initialSpread);
  }

  reset(initialSpread = false) {
    this.x = initialSpread ? Math.random() * (width + 200) - 100 : -40 - Math.random() * 100;
    this.y = Math.random() * (height * 0.85);
    this.scale = 0.5 + Math.random() * 0.7; // 景深大小
    this.baseSpeedX = (0.6 + Math.random() * 0.9) * this.scale;
    this.baseSpeedY = (Math.random() - 0.5) * 0.3;
    this.vx = this.baseSpeedX;
    this.vy = this.baseSpeedY;
    this.wobblePhase = Math.random() * Math.PI * 2;
    this.wobbleSpeed = 0.02 + Math.random() * 0.03;
    this.angle = (Math.random() - 0.5) * 0.4;
    this.angVel = (Math.random() - 0.5) * 0.01;
    this.opacity = 0.5 + Math.random() * 0.45;
    this.rays = 9 + Math.floor(Math.random() * 5); // 伞状冠毛数量
  }

  update(dt, windMultiplier) {
    this.wobblePhase += this.wobbleSpeed;
    const waveY = Math.sin(this.wobblePhase) * 0.4;

    // 鼠标风力推斥
    if (mouseWind.active) {
      const dx = this.x - mouseWind.x;
      const dy = this.y - mouseWind.y;
      const dist = Math.hypot(dx, dy);
      if (dist < mouseWind.radius && dist > 1) {
        const factor = (1 - dist / mouseWind.radius) * 3.5;
        this.vx += (dx / dist) * factor + (mouseWind.vx * 0.08);
        this.vy += (dy / dist) * factor - 0.5; // 气流向上扬起
        this.angVel += (Math.random() - 0.5) * 0.05;
      }
    }

    // 微风移动
    this.x += (this.vx + windMultiplier * 0.7) * (dt * 60);
    this.y += (this.vy + waveY) * (dt * 60);

    // 阻尼复原
    this.vx += (this.baseSpeedX - this.vx) * 0.02;
    this.vy += (this.baseSpeedY - this.vy) * 0.02;
    this.angVel *= 0.96;
    this.angle += this.angVel;

    // 出界重生
    if (this.x > width + 80 || this.y < -50 || this.y > height + 50) {
      this.reset(false);
    }
  }

  draw(context) {
    context.save();
    context.translate(this.x, this.y);
    context.rotate(this.angle + Math.sin(this.wobblePhase) * 0.15);
    context.scale(this.scale, this.scale);
    context.globalAlpha = this.opacity;

    // 1. 绘制小种子核 (棕褐小椭圆)
    context.fillStyle = '#4a382a';
    context.beginPath();
    context.ellipse(0, 14, 1.4, 3, 0, 0, Math.PI * 2);
    context.fill();

    // 2. 细小银白茎轴
    context.strokeStyle = 'rgba(235, 245, 240, 0.9)';
    context.lineWidth = 0.8;
    context.beginPath();
    context.moveTo(0, 13);
    context.lineTo(0, 0);
    context.stroke();

    // 3. 展开的伞状冠毛 (辐射细丝)
    context.strokeStyle = 'rgba(255, 255, 255, 0.85)';
    context.lineWidth = 0.6;
    const crownRadius = 14;
    for (let i = 0; i < this.rays; i++) {
      const spread = (i / (this.rays - 1) - 0.5) * (Math.PI * 0.8);
      const rx = Math.sin(spread) * crownRadius;
      const ry = -Math.cos(spread) * crownRadius * 0.75;
      context.beginPath();
      context.moveTo(0, 0);
      context.quadraticCurveTo(rx * 0.5, ry * 0.7, rx, ry);
      context.stroke();

      // 冠毛尖端微小绒白点
      context.fillStyle = 'rgba(255, 255, 255, 0.95)';
      context.beginPath();
      context.arc(rx, ry, 0.75, 0, Math.PI * 2);
      context.fill();
    }

    context.restore();
  }
}

// ----------------------------------------------------
// 2. 微风青草系统 (Wind-Swept Grass Blades)
// ----------------------------------------------------
class GrassBlade {
  constructor(x, baseY, height, width, colorBase, tipColor) {
    this.x = x;
    this.baseY = baseY;
    this.length = height;
    this.width = width;
    this.colorBase = colorBase;
    this.tipColor = tipColor;
    this.naturalAngle = (Math.random() - 0.5) * 0.15;
    this.currentBend = 0;
    this.targetBend = 0;
    this.stiffness = 0.08 + Math.random() * 0.04;
    this.hasFlower = Math.random() < 0.06; // 偶尔开出一朵淡雅雏菊
    this.flowerPetalColor = Math.random() > 0.4 ? 'rgba(255,255,255,0.95)' : 'rgba(254,240,138,0.9)';
  }

  update(time, windSpeed) {
    // 复合自然风波：基波 + 高频扰动
    const wave = Math.sin(time * (0.8 + windSpeed * 0.2) + this.x * 0.0035) * 0.22 * (windSpeed / 4);
    const microJitter = Math.cos(time * 2.2 + this.x * 0.01) * 0.04;
    this.targetBend = wave + microJitter + this.naturalAngle;

    // 鼠标经过带来的风浪压弯
    if (mouseWind.active) {
      const dx = this.x - mouseWind.x;
      const dy = (this.baseY - this.length * 0.7) - mouseWind.y;
      const dist = Math.hypot(dx, dy);
      if (dist < mouseWind.radius) {
        const influence = (1 - dist / mouseWind.radius);
        // 草叶受鼠标滑动方向及扩散推力而倒伏
        const pushDir = dx > 0 ? 1 : -1;
        const pushForce = pushDir * influence * 0.8 + (mouseWind.vx * 0.015);
        this.targetBend += pushForce;
      }
    }

    // 弹性平滑逼近
    this.currentBend += (this.targetBend - this.currentBend) * this.stiffness;
  }

  draw(context) {
    context.save();
    const tipX = this.x + Math.sin(this.currentBend) * this.length;
    const tipY = this.baseY - Math.cos(this.currentBend) * this.length;
    const ctrlX = this.x + Math.sin(this.currentBend * 0.5) * (this.length * 0.55);
    const ctrlY = this.baseY - this.length * 0.5;

    // 渐变草叶：根部深绿到尖端嫩翠黄绿
    const grad = context.createLinearGradient(this.x, this.baseY, tipX, tipY);
    grad.addColorStop(0, this.colorBase);
    grad.addColorStop(1, this.tipColor);

    context.fillStyle = grad;
    context.beginPath();
    context.moveTo(this.x - this.width * 0.5, this.baseY);
    context.quadraticCurveTo(ctrlX, ctrlY, tipX, tipY);
    context.quadraticCurveTo(ctrlX + this.width * 0.3, ctrlY, this.x + this.width * 0.5, this.baseY);
    context.closePath();
    context.fill();

    // 偶尔点缀的小野花
    if (this.hasFlower) {
      context.translate(tipX, tipY);
      context.rotate(this.currentBend);
      // 5瓣小雏菊
      context.fillStyle = this.flowerPetalColor;
      for (let p = 0; p < 5; p++) {
        context.beginPath();
        context.ellipse(0, -4, 1.8, 3.2, (p * Math.PI * 2) / 5, 0, Math.PI * 2);
        context.fill();
      }
      // 花蕊
      context.fillStyle = '#f59e0b';
      context.beginPath();
      context.arc(0, 0, 1.8, 0, Math.PI * 2);
      context.fill();
    }

    context.restore();
  }
}

// ----------------------------------------------------
// 3. 暖光光斑与轻柔飞瓣 (Sun Bokeh & Blossom Petals)
// ----------------------------------------------------
class FloatingBokeh {
  constructor() {
    this.reset();
  }

  reset() {
    this.x = Math.random() * width;
    this.y = Math.random() * height;
    this.radius = 16 + Math.random() * 48;
    this.vx = (0.2 + Math.random() * 0.4);
    this.vy = -0.15 - Math.random() * 0.3; // 缓缓升腾
    this.alpha = 0.08 + Math.random() * 0.18;
    this.phase = Math.random() * Math.PI * 2;
  }

  update(dt) {
    this.phase += 0.015;
    this.x += this.vx * (dt * 60);
    this.y += this.vy * (dt * 60);

    if (this.x > width + 100 || this.y < -100) {
      this.reset();
      this.y = height + 50;
      this.x = Math.random() * width;
    }
  }

  draw(context) {
    const pulse = 1 + Math.sin(this.phase) * 0.2;
    const currentAlpha = Math.max(0, this.alpha * (0.8 + Math.sin(this.phase) * 0.3));

    const g = context.createRadialGradient(
      this.x, this.y, 1,
      this.x, this.y, this.radius * pulse
    );
    g.addColorStop(0, `rgba(255, 252, 230, ${currentAlpha})`);
    g.addColorStop(0.6, `rgba(220, 245, 215, ${currentAlpha * 0.5})`);
    g.addColorStop(1, 'rgba(255, 255, 255, 0)');

    context.fillStyle = g;
    context.beginPath();
    context.arc(this.x, this.y, this.radius * pulse, 0, Math.PI * 2);
    context.fill();
  }
}

// 飘舞的柔粉/纯白花瓣
class FloatingPetal {
  constructor() {
    this.reset();
  }

  reset() {
    this.x = Math.random() * width;
    this.y = -20 - Math.random() * 100;
    this.size = 5 + Math.random() * 6;
    this.speedX = 0.8 + Math.random() * 1.2;
    this.speedY = 0.5 + Math.random() * 0.8;
    this.rot = Math.random() * Math.PI * 2;
    this.rotSpeed = 0.02 + Math.random() * 0.03;
    this.flip = 0;
    this.flipSpeed = 0.03 + Math.random() * 0.04;
    this.color = Math.random() > 0.3 ? 'rgba(255, 235, 240, 0.85)' : 'rgba(235, 250, 230, 0.85)';
  }

  update(dt, windMultiplier) {
    this.rot += this.rotSpeed;
    this.flip += this.flipSpeed;
    this.x += (this.speedX + windMultiplier * 0.6) * (dt * 60);
    this.y += (this.speedY + Math.sin(this.rot) * 0.3) * (dt * 60);

    if (this.x > width + 50 || this.y > height + 50) {
      this.reset();
    }
  }

  draw(context) {
    context.save();
    context.translate(this.x, this.y);
    context.rotate(this.rot);
    context.scale(1, Math.cos(this.flip)); // 3D 翻转模拟
    context.fillStyle = this.color;
    context.beginPath();
    context.ellipse(0, 0, this.size, this.size * 0.55, 0, 0, Math.PI * 2);
    context.fill();
    context.restore();
  }
}

// ----------------------------------------------------
// 4. 浮云与丁达尔光束渲染
// ----------------------------------------------------
let clouds = [];
function initClouds() {
  clouds = [
    { x: width * 0.1, y: height * 0.16, scale: 1.2, speed: 0.15, alpha: 0.45 },
    { x: width * 0.45, y: height * 0.09, scale: 0.85, speed: 0.1, alpha: 0.35 },
    { x: width * 0.78, y: height * 0.18, scale: 1.1, speed: 0.18, alpha: 0.4 }
  ];
}

function drawCloud(context, cloud) {
  context.save();
  context.translate(cloud.x, cloud.y);
  context.scale(cloud.scale, cloud.scale);
  context.fillStyle = `rgba(255, 255, 255, ${cloud.alpha})`;

  // 由多个重叠的软圆聚合成蓬松云朵
  const puffs = [
    { dx: 0, dy: 0, r: 45 },
    { dx: 35, dy: -12, r: 40 },
    { dx: 70, dy: 2, r: 35 },
    { dx: -35, dy: 5, r: 32 },
    { dx: 20, dy: 10, r: 38 }
  ];
  for (const p of puffs) {
    context.beginPath();
    context.arc(p.dx, p.dy, p.r, 0, Math.PI * 2);
    context.fill();
  }
  context.restore();
}

// 丁达尔神光 (God Rays)
function drawSunbeams(context, time) {
  const intensity = config.sunbeamIntensity / 10;
  if (intensity <= 0.05) return;

  const sunX = width * 0.12;
  const sunY = height * 0.08;

  // 太阳本体与柔和光晕
  const sunGlow = context.createRadialGradient(sunX, sunY, 10, sunX, sunY, width * 0.45);
  sunGlow.addColorStop(0, `rgba(255, 253, 240, ${0.45 * intensity})`);
  sunGlow.addColorStop(0.3, `rgba(254, 243, 199, ${0.25 * intensity})`);
  sunGlow.addColorStop(0.7, `rgba(224, 242, 254, ${0.1 * intensity})`);
  sunGlow.addColorStop(1, 'rgba(255, 255, 255, 0)');
  context.fillStyle = sunGlow;
  context.fillRect(0, 0, width, height);

  // 倾斜光束
  context.save();
  const beamAngles = [0.45, 0.58, 0.72, 0.88, 1.05];
  for (let i = 0; i < beamAngles.length; i++) {
    const baseAngle = beamAngles[i];
    const breathe = Math.sin(time * 0.8 + i * 1.5) * 0.05;
    const beamAngle = baseAngle + breathe;
    const beamWidth = (40 + i * 18);
    const beamAlpha = (0.08 + Math.sin(time * 0.5 + i) * 0.03 + audioVolume * 0.1) * intensity;

    const len = height * 1.4;
    const endX1 = sunX + Math.cos(beamAngle - 0.04) * len;
    const endY1 = sunY + Math.sin(beamAngle - 0.04) * len;
    const endX2 = sunX + Math.cos(beamAngle + 0.04) * len + beamWidth;
    const endY2 = sunY + Math.sin(beamAngle + 0.04) * len;

    const beamGrad = context.createLinearGradient(sunX, sunY, (endX1 + endX2) * 0.5, (endY1 + endY2) * 0.5);
    beamGrad.addColorStop(0, `rgba(255, 255, 245, ${beamAlpha * 1.8})`);
    beamGrad.addColorStop(0.5, `rgba(254, 249, 195, ${beamAlpha})`);
    beamGrad.addColorStop(1, 'rgba(255, 255, 255, 0)');

    context.fillStyle = beamGrad;
    context.beginPath();
    context.moveTo(sunX, sunY);
    context.lineTo(endX1, endY1);
    context.lineTo(endX2, endY2);
    context.closePath();
    context.fill();
  }
  context.restore();
}

// ----------------------------------------------------
// 5. 远山叠峦与晨雾层次 (Layered Mountains & Valley Mist)
// ----------------------------------------------------
function drawMountains(context, time) {
  // 第 1 层远山：淡天青蓝绿
  context.fillStyle = 'rgba(176, 208, 206, 0.55)';
  context.beginPath();
  context.moveTo(0, height * 0.62);
  context.bezierCurveTo(width * 0.25, height * 0.52, width * 0.45, height * 0.64, width * 0.7, height * 0.54);
  context.bezierCurveTo(width * 0.85, height * 0.48, width * 0.95, height * 0.56, width, height * 0.52);
  context.lineTo(width, height);
  context.lineTo(0, height);
  context.closePath();
  context.fill();

  // 晨雾漫游带
  const mistShift = Math.sin(time * 0.2) * 40;
  const mistGrad = context.createLinearGradient(0, height * 0.55, 0, height * 0.68);
  mistGrad.addColorStop(0, 'rgba(240, 252, 248, 0)');
  mistGrad.addColorStop(0.5, 'rgba(240, 252, 248, 0.45)');
  mistGrad.addColorStop(1, 'rgba(240, 252, 248, 0)');
  context.fillStyle = mistGrad;
  context.fillRect(0, height * 0.52, width, height * 0.16);

  // 第 2 层中远山：苍黛青绿
  context.fillStyle = 'rgba(125, 172, 154, 0.75)';
  context.beginPath();
  context.moveTo(0, height * 0.68);
  context.bezierCurveTo(width * 0.2, height * 0.58, width * 0.4, height * 0.72, width * 0.65, height * 0.62);
  context.bezierCurveTo(width * 0.82, height * 0.55, width * 0.92, height * 0.66, width, height * 0.63);
  context.lineTo(width, height);
  context.lineTo(0, height);
  context.closePath();
  context.fill();

  // 第 3 层近景起伏草坡 A (翠绿带柔光)
  const hillGradA = context.createLinearGradient(0, height * 0.65, 0, height);
  hillGradA.addColorStop(0, '#7bb883');
  hillGradA.addColorStop(0.5, '#559960');
  hillGradA.addColorStop(1, '#346b3e');
  context.fillStyle = hillGradA;
  context.beginPath();
  context.moveTo(0, height * 0.76);
  context.bezierCurveTo(width * 0.3, height * 0.68, width * 0.65, height * 0.82, width, height * 0.73);
  context.lineTo(width, height);
  context.lineTo(0, height);
  context.closePath();
  context.fill();

  // 第 4 层前景起伏草坡 B (生机苍翠)
  const hillGradB = context.createLinearGradient(0, height * 0.78, 0, height);
  hillGradB.addColorStop(0, '#5fa868');
  hillGradB.addColorStop(0.4, '#40824a');
  hillGradB.addColorStop(1, '#23522c');
  context.fillStyle = hillGradB;
  context.beginPath();
  context.moveTo(0, height * 0.84);
  context.bezierCurveTo(width * 0.35, height * 0.88, width * 0.7, height * 0.78, width, height * 0.83);
  context.lineTo(width, height);
  context.lineTo(0, height);
  context.closePath();
  context.fill();
}

// ----------------------------------------------------
// 6. 场景初始化与实体池管理
// ----------------------------------------------------
let grassBlades = [];
let dandelions = [];
let bokehs = [];
let petals = [];

function initScene() {
  width = canvas.width = window.innerWidth;
  height = canvas.height = window.innerHeight;
  dpr = Math.min(window.devicePixelRatio || 1, 2);

  // 初始化浮云
  initClouds();

  // 1. 初始化草叶：分两排致密覆盖屏幕底部草坡
  grassBlades = [];
  const bladeCount = Math.floor(width / 3.8); // 视口自适应密度
  for (let i = 0; i < bladeCount; i++) {
    const x = (i / bladeCount) * width + (Math.random() - 0.5) * 8;
    const isFront = Math.random() > 0.45;
    
    // 依草坡曲线计算 baseY
    const hillHeightFactor = isFront ? 0.84 : 0.77;
    const curveOffset = Math.sin((x / width) * Math.PI) * (height * 0.06);
    const baseY = height * hillHeightFactor + curveOffset + Math.random() * (height * 0.15);
    
    const bladeLen = (isFront ? 65 : 45) + Math.random() * 45;
    const bladeW = (isFront ? 3.0 : 2.2) + Math.random() * 1.5;

    // 色彩梯度：橄榄深青 -> 嫩草黄绿
    const baseColor = isFront ? '#265430' : '#3d7248';
    const tipColor = isFront ? '#a7f38d' : '#82d874';

    grassBlades.push(new GrassBlade(x, baseY, bladeLen, bladeW, baseColor, tipColor));
  }

  // 2. 初始化蒲公英
  dandelions = [];
  for (let i = 0; i < config.dandelionCount; i++) {
    dandelions.push(new DandelionSeed(true));
  }

  // 3. 暖光光斑
  bokehs = [];
  for (let i = 0; i < 20; i++) {
    bokehs.push(new FloatingBokeh());
  }

  // 4. 飘飞花瓣
  petals = [];
  for (let i = 0; i < 12; i++) {
    petals.push(new FloatingPetal());
  }
}

// ----------------------------------------------------
// 7. 主渲染管线循环 (Main Render Loop)
// ----------------------------------------------------
function render(now) {
  if (!isRunning) return;

  const dt = Math.min((now - lastTime) / 1000, 0.1);
  lastTime = now;
  globalTime += dt;

  // 微风强度参数
  const currentWindSpeed = config.windSpeed + (audioVolume * 6);

  // 1. 绘制清爽渐变天际线
  const skyGrad = ctx.createLinearGradient(0, 0, 0, height * 0.8);
  skyGrad.addColorStop(0, '#b8e3f2');    // 清爽蔚蓝
  skyGrad.addColorStop(0.35, '#d4edf2'); // 柔和天青
  skyGrad.addColorStop(0.7, '#fef5e4');  // 晨光浅金
  skyGrad.addColorStop(1, '#d8f0dc');    // 薄荷青草底色
  ctx.fillStyle = skyGrad;
  ctx.fillRect(0, 0, width, height);

  // 2. 丁达尔晨光透镜
  drawSunbeams(ctx, globalTime);

  // 3. 浮云漫游
  for (const c of clouds) {
    c.x += (c.speed + currentWindSpeed * 0.08) * (dt * 60);
    if (c.x > width + 150) c.x = -150;
    drawCloud(ctx, c);
  }

  // 4. 远山叠峦与谷间晨雾
  drawMountains(ctx, globalTime);

  // 5. 景深层 A：背景漂浮的暖光散景
  for (const b of bokehs) {
    b.update(dt);
    b.draw(ctx);
  }

  // 6. 微风草叶起伏摇曳
  for (const blade of grassBlades) {
    blade.update(globalTime, currentWindSpeed);
    blade.draw(ctx);
  }

  // 7. 蒲公英飞絮飘舞
  for (const seed of dandelions) {
    seed.update(dt, currentWindSpeed);
    seed.draw(ctx);
  }

  // 8. 飘落花瓣
  for (const p of petals) {
    p.update(dt, currentWindSpeed);
    p.draw(ctx);
  }

  // 鼠标交互气流衰减
  if (mouseWind.active) {
    mouseWind.vx *= 0.92;
    mouseWind.vy *= 0.92;
    if (Math.abs(mouseWind.vx) < 0.1 && Math.abs(mouseWind.vy) < 0.1) {
      mouseWind.vx = 0;
      mouseWind.vy = 0;
    }
  }

  animationFrameId = requestAnimationFrame(render);
}

// ----------------------------------------------------
// 8. 交互事件与 LinDesk 原生协议集成
// ----------------------------------------------------

// 产生微风冲击波（点击或 mousedown 时爆发蒲公英）
function triggerWindBurst(x, y) {
  mouseWind.x = x;
  mouseWind.y = y;
  mouseWind.active = true;
  mouseWind.radius = 350;

  // 瞬间激发出 6~10 枚新生蒲公英飞向高空
  const burstCount = 8;
  for (let i = 0; i < burstCount; i++) {
    const seed = new DandelionSeed(false);
    seed.x = x + (Math.random() - 0.5) * 60;
    seed.y = y + (Math.random() - 0.5) * 60;
    const angle = Math.random() * Math.PI * 2;
    const force = 3 + Math.random() * 4;
    seed.vx = Math.cos(angle) * force + 1.5;
    seed.vy = Math.sin(angle) * force - 2.5; // 整体向上扬起
    dandelions.push(seed);
  }

  // 保持数组大小在合理范围内
  if (dandelions.length > config.dandelionCount + 24) {
    dandelions.splice(0, dandelions.length - (config.dandelionCount + 24));
  }

  setTimeout(() => {
    mouseWind.radius = 220;
  }, 400);
}

// 监听 LinDesk 宿主 IPC 消息 (CEF / Hub Bridge)
window.addEventListener('message', (event) => {
  const msg = event.data;
  if (!msg || typeof msg !== 'object') return;

  switch (msg.type) {
    // 智能休眠与能效挂起
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
        lastTime = performance.now();
        animationFrameId = requestAnimationFrame(render);
      }
      break;

    // 桌面原生穿透鼠标移动
    case 'desktop_mousemove':
      if (mouseWind.active) {
        mouseWind.vx = (msg.x - mouseWind.x) * 0.5;
        mouseWind.vy = (msg.y - mouseWind.y) * 0.5;
      }
      mouseWind.x = msg.x;
      mouseWind.y = msg.y;
      mouseWind.active = true;
      break;

    case 'desktop_mousedown':
      triggerWindBurst(msg.x || width * 0.5, msg.y || height * 0.5);
      break;

    // 壁纸属性热调节 (来自 WallpaperStudio / Hub)
    case 'wallpaper_property_change':
    case 'set_property':
      const prop = msg.property || msg.key;
      const val = msg.value;
      if (prop === 'windSpeed' && typeof val === 'number') {
        config.windSpeed = Math.max(1, Math.min(10, val));
      } else if (prop === 'dandelionCount' && typeof val === 'number') {
        config.dandelionCount = Math.max(10, Math.min(80, val));
        while (dandelions.length < config.dandelionCount) dandelions.push(new DandelionSeed(true));
        if (dandelions.length > config.dandelionCount) dandelions.length = config.dandelionCount;
      } else if (prop === 'sunbeamIntensity' && typeof val === 'number') {
        config.sunbeamIntensity = Math.max(0, Math.min(10, val));
      } else if (prop === 'showClock' && typeof val === 'boolean') {
        config.showClock = val;
        const hud = document.getElementById('hud-overlay');
        if (hud) hud.style.display = val ? 'block' : 'none';
      }
      break;

    // 音乐律动
    case 'audio_data':
      if (Array.isArray(msg.frequencies)) {
        audioFrequencies = msg.frequencies;
        const sum = audioFrequencies.reduce((a, b) => a + b, 0);
        audioVolume = sum / audioFrequencies.length;
      }
      break;
  }
});

// 本地网页预览 / 调试环境事件支持
window.addEventListener('mousemove', (e) => {
  if (mouseWind.active) {
    mouseWind.vx = (e.clientX - mouseWind.x) * 0.6;
    mouseWind.vy = (e.clientY - mouseWind.y) * 0.6;
  }
  mouseWind.x = e.clientX;
  mouseWind.y = e.clientY;
  mouseWind.active = true;
});

window.addEventListener('mousedown', (e) => {
  triggerWindBurst(e.clientX, e.clientY);
});

window.addEventListener('mouseleave', () => {
  mouseWind.active = false;
});

window.addEventListener('resize', () => {
  initScene();
});

// ----------------------------------------------------
// 9. 极简环境时钟更新器
// ----------------------------------------------------
function updateClock() {
  const now = new Date();
  const timeEl = document.getElementById('clock-time');
  const dateEl = document.getElementById('clock-date');

  if (timeEl) {
    const hours = String(now.getHours()).padStart(2, '0');
    const minutes = String(now.getMinutes()).padStart(2, '0');
    timeEl.textContent = `${hours}:${minutes}`;
  }

  if (dateEl) {
    const options = { weekday: 'long', month: 'short', day: 'numeric' };
    dateEl.textContent = now.toLocaleDateString('zh-CN', options);
  }
}

// 启动引擎
setInterval(updateClock, 1000);
updateClock();
initScene();
lastTime = performance.now();
animationFrameId = requestAnimationFrame(render);
