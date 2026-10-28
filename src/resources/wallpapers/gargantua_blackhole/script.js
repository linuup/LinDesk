(function () {
  'use strict';

  const canvas = document.getElementById('wallpaper-canvas');
  const ctx = canvas.getContext('2d');

  let width = 0;
  let height = 0;
  let dpr = 1;

  // 观察视角与倾角
  const tilt = {
    currentX: 0,
    currentY: 0,
    targetX: 0,
    targetY: 0
  };

  const mouse = {
    x: -9999,
    y: -9999,
    active: false,
    radius: 180
  };

  // 尺寸自适应与黑洞参数
  let blackHole = {
    cx: 0,
    cy: 0,
    radius: 95,          // 视界半径 (Event horizon)
    photonRadius: 130,   // 光子球半径
    diskInner: 170,      // 吸积盘内径
    diskOuter: 380,      // 吸积盘外径
    diskTilt: 0.28       // 吸积盘椭圆垂直压扁比
  };

  function updateDimensions() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    width = window.innerWidth;
    height = window.innerHeight;
    canvas.width = Math.floor(width * dpr);
    canvas.height = Math.floor(height * dpr);
    ctx.scale(dpr, dpr);

    blackHole.cx = width * 0.5;
    blackHole.cy = height * 0.52;

    const baseDim = Math.min(width, height);
    blackHole.radius = Math.max(70, baseDim * 0.11);
    blackHole.photonRadius = blackHole.radius * 1.34;
    blackHole.diskInner = blackHole.radius * 1.75;
    blackHole.diskOuter = blackHole.radius * 4.2;
  }

  // 1. 背景恒星系统（带引力透镜扭曲支持）
  const STAR_COUNT = 320;
  const stars = [];

  function initStars() {
    stars.length = 0;
    for (let i = 0; i < STAR_COUNT; i++) {
      stars.push({
        origX: Math.random() * width,
        origY: Math.random() * height,
        baseRadius: Math.random() * 1.3 + 0.4,
        alpha: Math.random() * 0.7 + 0.3,
        twinkleSpeed: Math.random() * 0.03 + 0.01,
        twinklePhase: Math.random() * Math.PI * 2,
        color: ['#ffffff', '#bae6fd', '#fed7aa', '#fde047', '#e0f2fe'][Math.floor(Math.random() * 5)]
      });
    }
  }

  // 2. 吸积盘高密度等离子等流体丝（Plasma Streamers）
  const STREAMER_COUNT = 480;
  const streamers = [];

  function initStreamers() {
    streamers.length = 0;
    for (let i = 0; i < STREAMER_COUNT; i++) {
      // 距离中心的归一化半径 (0 ~ 1)
      const u = Math.pow(Math.random(), 1.6);
      const rad = blackHole.diskInner + u * (blackHole.diskOuter - blackHole.diskInner);
      streamers.push({
        radFraction: u,
        angle: Math.random() * Math.PI * 2,
        speed: (0.018 / (u * 1.8 + 0.4)) * (Math.random() * 0.25 + 0.88),
        size: Math.random() * 2.8 + 1.2,
        alpha: Math.random() * 0.65 + 0.35,
        streakLen: Math.random() * 0.15 + 0.08,
        tempOffset: (Math.random() - 0.5) * 0.2
      });
    }
  }

  // 3. 吸积尘埃微粒（螺旋向内坠入黑洞）
  const FALLING_DUST_COUNT = 160;
  const fallingDust = [];

  function initDust() {
    fallingDust.length = 0;
    for (let i = 0; i < FALLING_DUST_COUNT; i++) {
      resetDustParticle(fallingDust[i] = {});
    }
  }

  function resetDustParticle(p) {
    p.radius = blackHole.diskOuter * (Math.random() * 0.6 + 0.9);
    p.angle = Math.random() * Math.PI * 2;
    p.speed = Math.random() * 0.012 + 0.008;
    p.inwardSpeed = Math.random() * 0.45 + 0.15;
    p.size = Math.random() * 2.0 + 0.8;
    p.alpha = Math.random() * 0.5 + 0.3;
    p.color = Math.random() > 0.4 ? 'rgba(254, 215, 170,' : 'rgba(186, 230, 253,';
  }

  // 4. 引力波冲击波池
  const ripples = [];
  function addGravitationalWave(x, y) {
    ripples.push({
      x: x,
      y: y,
      radius: 10,
      maxRadius: Math.max(width, height) * 0.85,
      alpha: 0.9,
      speed: 12
    });
  }

  // 颜色根据相对论多普勒频移动态插值 (0: 背向观察者暗红，0.5: 纯白热，1: 面向观察者高光蓝白)
  function getDopplerColor(factor, alpha) {
    factor = Math.max(0, Math.min(1, factor));
    let r, g, b;
    if (factor < 0.45) {
      // 橙红 -> 金黄
      const t = factor / 0.45;
      r = Math.floor(240 + 15 * t);
      g = Math.floor(80 + 110 * t);
      b = Math.floor(20 + 40 * t);
    } else if (factor < 0.8) {
      // 金黄 -> 白热
      const t = (factor - 0.45) / 0.35;
      r = 255;
      g = Math.floor(190 + 65 * t);
      b = Math.floor(60 + 195 * t);
    } else {
      // 强高能蓝白高光
      const t = (factor - 0.8) / 0.2;
      r = Math.floor(255 - 40 * t);
      g = Math.floor(255 - 15 * t);
      b = 255;
    }
    return `rgba(${r}, ${g}, ${b}, ${alpha.toFixed(3)})`;
  }

  // 渲染函数
  let time = 0;

  function render() {
    time += 0.016;

    // 缓动平滑视角倾斜
    tilt.currentX += (tilt.targetX - tilt.currentX) * 0.05;
    tilt.currentY += (tilt.targetY - tilt.currentY) * 0.05;

    const centerX = blackHole.cx + tilt.currentX * 35;
    const centerY = blackHole.cy + tilt.currentY * 20;

    ctx.clearRect(0, 0, width, height);

    // ==========================================
    // 1. 深空星云背景微光 (Nebula Glow)
    // ==========================================
    const bgGrad = ctx.createRadialGradient(
      centerX, centerY, blackHole.radius * 0.5,
      centerX, centerY, Math.max(width, height) * 0.75
    );
    bgGrad.addColorStop(0, '#040711');
    bgGrad.addColorStop(0.35, '#070b18');
    bgGrad.addColorStop(0.7, '#05070e');
    bgGrad.addColorStop(1, '#020306');
    ctx.fillStyle = bgGrad;
    ctx.fillRect(0, 0, width, height);

    // ==========================================
    // 2. 引力透镜弯曲背景恒星 (Lensed Stars)
    // ==========================================
    ctx.save();
    for (let i = 0; i < stars.length; i++) {
      const s = stars[i];
      let dx = s.origX - centerX;
      let dy = s.origY - centerY;
      let dist = Math.sqrt(dx * dx + dy * dy);

      // 爱因斯坦引力透镜弯曲位移
      const einsteinRadius = blackHole.photonRadius * 1.55;
      if (dist > 5) {
        const deflection = (einsteinRadius * einsteinRadius) / (dist * 1.4);
        const lensedDist = dist + deflection;
        const ratio = lensedDist / dist;
        dx *= ratio;
        dy *= ratio;
      }

      // 靠近视界的恒星被吸积吞噬隐藏
      const currentDist = Math.sqrt(dx * dx + dy * dy);
      if (currentDist < blackHole.radius * 1.05) {
        continue;
      }

      // 闪烁
      const twinkle = Math.sin(time * 2 + s.twinklePhase) * 0.25 + 0.75;
      const finalAlpha = s.alpha * twinkle * (currentDist < blackHole.radius * 1.4 ? (currentDist - blackHole.radius) / (blackHole.radius * 0.4) : 1);

      ctx.fillStyle = s.color;
      ctx.globalAlpha = Math.max(0, Math.min(1, finalAlpha));
      ctx.beginPath();
      ctx.arc(centerX + dx, centerY + dy, s.baseRadius, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();

    // ==========================================
    // 3. 上层弯曲光弧（背部吸积盘向上弯折光冠 Upper Arc）
    // ==========================================
    ctx.save();
    ctx.globalCompositeOperation = 'screen';

    // 绘制黑洞顶部的引力透镜光环 (广义相对论时空弯曲特效)
    const upperArcGrad = ctx.createRadialGradient(
      centerX, centerY, blackHole.radius * 0.95,
      centerX, centerY, blackHole.photonRadius * 2.1
    );
    upperArcGrad.addColorStop(0, 'rgba(0, 0, 0, 0)');
    upperArcGrad.addColorStop(0.2, 'rgba(255, 240, 200, 0.95)');
    upperArcGrad.addColorStop(0.45, 'rgba(251, 146, 60, 0.75)');
    upperArcGrad.addColorStop(0.75, 'rgba(234, 88, 12, 0.35)');
    upperArcGrad.addColorStop(1, 'rgba(124, 45, 18, 0)');

    ctx.fillStyle = upperArcGrad;
    ctx.beginPath();
    // 绘制马蹄形顶部弯曲光弧
    ctx.ellipse(centerX, centerY - blackHole.radius * 0.2, blackHole.photonRadius * 1.85, blackHole.photonRadius * 1.5, 0, Math.PI * 0.95, Math.PI * 2.05, false);
    ctx.fill();

    // 下方微弱引力透镜倒影光弧 (Lower Arc)
    ctx.beginPath();
    ctx.ellipse(centerX, centerY + blackHole.radius * 0.25, blackHole.photonRadius * 1.7, blackHole.photonRadius * 0.9, 0, 0, Math.PI, false);
    ctx.fillStyle = 'rgba(234, 88, 12, 0.28)';
    ctx.fill();
    ctx.restore();

    // ==========================================
    // 4. 正面炽热吸积盘主盘体 (Main Accretion Disk)
    // ==========================================
    ctx.save();
    ctx.globalCompositeOperation = 'screen';

    const diskYRatio = blackHole.diskTilt + tilt.currentY * 0.08;

    // 绘制吸积盘背景大范围柔和光晕
    const glowGrad = ctx.createRadialGradient(
      centerX, centerY, blackHole.diskInner * 0.6,
      centerX, centerY, blackHole.diskOuter * 1.15
    );
    glowGrad.addColorStop(0, 'rgba(255, 230, 180, 0.45)');
    glowGrad.addColorStop(0.25, 'rgba(249, 115, 22, 0.4)');
    glowGrad.addColorStop(0.65, 'rgba(194, 65, 12, 0.18)');
    glowGrad.addColorStop(1, 'rgba(15, 23, 42, 0)');

    ctx.fillStyle = glowGrad;
    ctx.beginPath();
    ctx.ellipse(centerX, centerY, blackHole.diskOuter * 1.25, blackHole.diskOuter * diskYRatio * 1.3, tilt.currentX * 0.05, 0, Math.PI * 2);
    ctx.fill();

    // 绘制高密度高速旋转等离子流体束
    for (let i = 0; i < streamers.length; i++) {
      const s = streamers[i];
      s.angle += s.speed;

      const curRadius = blackHole.diskInner + s.radFraction * (blackHole.diskOuter - blackHole.diskInner);
      const px = centerX + Math.cos(s.angle) * curRadius;
      const py = centerY + Math.sin(s.angle) * curRadius * diskYRatio;

      // 多普勒频移：左侧朝着观察者运动（负X）多普勒蓝移加亮，右侧远离红移变暗
      const dopplerFactor = ((-Math.sin(s.angle) + 1) * 0.5) + (s.tempOffset || 0);
      const streamerAlpha = s.alpha * (0.45 + dopplerFactor * 0.55);

      ctx.strokeStyle = getDopplerColor(dopplerFactor, streamerAlpha);
      ctx.lineWidth = s.size;
      ctx.beginPath();
      ctx.arc(centerX, centerY, curRadius, s.angle, s.angle + s.streakLen, false);
      ctx.stroke();
    }
    ctx.restore();

    // ==========================================
    // 5. 正在螺旋坠入视界的尘埃星流 (Infalling Dust)
    // ==========================================
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    for (let i = 0; i < fallingDust.length; i++) {
      const d = fallingDust[i];
      d.angle += d.speed;
      d.radius -= d.inwardSpeed;

      // 加速坠落效果
      if (d.radius < blackHole.photonRadius * 1.5) {
        d.speed *= 1.015;
        d.inwardSpeed *= 1.02;
      }

      if (d.radius <= blackHole.radius * 0.98) {
        resetDustParticle(d);
        continue;
      }

      const dx = Math.cos(d.angle) * d.radius;
      const dy = Math.sin(d.angle) * d.radius * (blackHole.diskTilt + tilt.currentY * 0.05);

      // 鼠标交互引力排斥与吸引
      let finalX = centerX + dx;
      let finalY = centerY + dy;

      if (mouse.active) {
        const mdx = finalX - mouse.x;
        const mdy = finalY - mouse.y;
        const mdist = Math.sqrt(mdx * mdx + mdy * mdy);
        if (mdist < mouse.radius && mdist > 1) {
          const force = (1 - mdist / mouse.radius) * 18;
          finalX += (mdx / mdist) * force;
          finalY += (mdy / mdist) * force;
        }
      }

      ctx.fillStyle = d.color + d.alpha.toFixed(2) + ')';
      ctx.beginPath();
      ctx.arc(finalX, finalY, d.size, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();

    // ==========================================
    // 6. 光子球边缘耀环 (Photon Sphere Ring)
    // ==========================================
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.95)';
    ctx.lineWidth = 2.4;
    ctx.shadowColor = '#fdba74';
    ctx.shadowBlur = 18;
    ctx.beginPath();
    ctx.arc(centerX, centerY, blackHole.photonRadius, 0, Math.PI * 2);
    ctx.stroke();

    // 第二重光晕微环
    ctx.strokeStyle = 'rgba(251, 146, 60, 0.65)';
    ctx.lineWidth = 1.2;
    ctx.shadowBlur = 32;
    ctx.beginPath();
    ctx.arc(centerX, centerY, blackHole.photonRadius * 1.06, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();

    // ==========================================
    // 7. 黑洞事件视界暗影 (The Event Horizon Shadow)
    // ==========================================
    ctx.save();
    ctx.fillStyle = '#010204';
    ctx.beginPath();
    ctx.arc(centerX, centerY, blackHole.radius * 1.02, 0, Math.PI * 2);
    ctx.fill();

    // 视界边缘极窄的引力渐变遮罩 (保持绝对纯黑的边缘过渡)
    const horizonEdge = ctx.createRadialGradient(
      centerX, centerY, blackHole.radius * 0.95,
      centerX, centerY, blackHole.radius * 1.04
    );
    horizonEdge.addColorStop(0, '#000000');
    horizonEdge.addColorStop(0.85, 'rgba(0, 0, 0, 0.98)');
    horizonEdge.addColorStop(1, 'rgba(0, 0, 0, 0)');
    ctx.fillStyle = horizonEdge;
    ctx.beginPath();
    ctx.arc(centerX, centerY, blackHole.radius * 1.06, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    // ==========================================
    // 8. 交互引力波涟漪渲染 (Gravitational Wave Ripples)
    // ==========================================
    if (ripples.length > 0) {
      ctx.save();
      ctx.globalCompositeOperation = 'screen';
      for (let i = ripples.length - 1; i >= 0; i--) {
        const r = ripples[i];
        r.radius += r.speed;
        r.alpha -= 0.012;

        if (r.alpha <= 0 || r.radius >= r.maxRadius) {
          ripples.splice(i, 1);
          continue;
        }

        ctx.strokeStyle = `rgba(186, 230, 253, ${(r.alpha * 0.6).toFixed(3)})`;
        ctx.lineWidth = 2.0;
        ctx.beginPath();
        ctx.arc(r.x, r.y, r.radius, 0, Math.PI * 2);
        ctx.stroke();

        ctx.strokeStyle = `rgba(251, 146, 60, ${(r.alpha * 0.35).toFixed(3)})`;
        ctx.lineWidth = 4.0;
        ctx.beginPath();
        ctx.arc(r.x, r.y, r.radius * 0.95, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.restore();
    }

    requestAnimationFrame(render);
  }

  // 鼠标交互监听
  window.addEventListener('mousemove', function (e) {
    mouse.x = e.clientX;
    mouse.y = e.clientY;
    mouse.active = true;

    // 根据鼠标相对于屏幕中心的位置产生三维视差偏移
    tilt.targetX = (e.clientX / width - 0.5) * 2;
    tilt.targetY = (e.clientY / height - 0.5) * 2;
  });

  window.addEventListener('mouseleave', function () {
    mouse.active = false;
    tilt.targetX = 0;
    tilt.targetY = 0;
  });

  window.addEventListener('mousedown', function (e) {
    addGravitationalWave(e.clientX, e.clientY);
  });

  window.addEventListener('resize', function () {
    updateDimensions();
    initStars();
    initStreamers();
    initDust();
  });

  // 初始化并启动引擎
  updateDimensions();
  initStars();
  initStreamers();
  initDust();
  requestAnimationFrame(render);

})();
