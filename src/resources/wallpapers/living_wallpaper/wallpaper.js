(function () {
  'use strict';

  // 1. LinWidget Bridge 通信层
  function sendMessage(type, payload) {
    const msg = Object.assign({ type: type }, payload || {});
    fetch('http://linwidget.bridge/postMessage', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(msg)
    }).catch(function () {
      try {
        window.parent.postMessage(msg, '*');
      } catch (e) {}
    });
  }

  // 2. DOM 元素获取
  const canvas = document.getElementById('bg-canvas');
  const ctx = canvas.getContext('2d');

  let width = 0;
  let height = 0;
  let time = 0;
  let lastTimestamp = performance.now();
  let isInteractive = true;

  // 交互状态
  const mouse = {
    x: -1000,
    y: -1000,
    isDown: false,
    downStartTime: 0,
    holdDuration: 0,
    hasMoved: false
  };

  // 冲击波与水波涟漪特效池
  const ripples = [];
  function addRipple(x, y, maxR, color, speed) {
    ripples.push({
      x: x,
      y: y,
      r: 5,
      maxR: maxR || 260,
      color: color || 'rgba(14, 165, 233,',
      alpha: 0.85,
      speed: speed || 7
    });
  }

  // 爆发火花粒子池
  const sparkParticles = [];
  function spawnSparks(x, y, count, rgb) {
    for (let i = 0; i < count; i++) {
      const angle = Math.random() * Math.PI * 2;
      const speed = Math.random() * 8 + 3;
      sparkParticles.push({
        x: x,
        y: y,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        size: Math.random() * 2.5 + 1.2,
        alpha: 1,
        decay: Math.random() * 0.03 + 0.018,
        rgb: rgb || [14, 165, 233]
      });
    }
  }

  // 流星系统
  const meteors = [];
  function spawnMeteor() {
    if (meteors.length >= 2) return;
    const startX = Math.random() * width * 1.2;
    const startY = Math.random() * height * 0.3;
    const length = Math.random() * 110 + 70;
    const speed = Math.random() * 8 + 10;
    const angle = Math.PI / 4 + (Math.random() - 0.5) * 0.2;
    meteors.push({
      x: startX,
      y: startY,
      dx: Math.cos(angle) * speed,
      dy: Math.sin(angle) * speed,
      len: length,
      alpha: 1,
      decay: 0.016
    });
  }

  // 3. 深空引力粒子群 (Cosmic Starfield)
  const cosmicStars = [];
  let COSMIC_STAR_COUNT = 90;
  let singularityColorHex = '#8b5cf6';
  let singularityRgb = [139, 92, 246];
  let fluidSpeedMultiplier = 1.0;
  let reactivePhysics = true;

  function hexToRgb(hex) {
    if (typeof hex !== 'string') return [139, 92, 246];
    let c = hex.replace('#', '');
    if (c.length === 3) c = c[0] + c[0] + c[1] + c[1] + c[2] + c[2];
    const num = parseInt(c, 16);
    return isNaN(num) ? [139, 92, 246] : [(num >> 16) & 255, (num >> 8) & 255, num & 255];
  }

  function handleWallpaperProperty(key, val) {
    if (key === 'particleCount') {
      const count = parseInt(val, 10);
      if (!isNaN(count) && count > 0) {
        COSMIC_STAR_COUNT = count;
        while (cosmicStars.length < count) {
          cosmicStars.push(new CosmicStar());
        }
        if (cosmicStars.length > count) {
          cosmicStars.length = count;
        }
      }
    } else if (key === 'fluidSpeed') {
      const spd = parseFloat(val);
      if (!isNaN(spd) && spd > 0) {
        fluidSpeedMultiplier = spd / 3.0;
      }
    } else if (key === 'singularityColor') {
      singularityColorHex = val;
      singularityRgb = hexToRgb(val);
    } else if (key === 'reactiveMouseWave') {
      reactivePhysics = !!val;
    }
  }

  class CosmicStar {
    constructor() {
      this.init(true);
    }
    init(randomize) {
      this.x = randomize ? Math.random() * (width || window.innerWidth) : Math.random() * width;
      this.y = randomize ? Math.random() * (height || window.innerHeight) : Math.random() * height;
      this.vx = (Math.random() - 0.5) * 0.45;
      this.vy = (Math.random() - 0.5) * 0.45;
      this.baseSize = Math.random() * 2.0 + 0.8;
      this.size = this.baseSize;
      this.twinklePhase = Math.random() * Math.PI * 2;
      this.twinkleSpeed = Math.random() * 0.03 + 0.012;
      this.isHot = Math.random() > 0.65;
      this.rgb = this.isHot ? [14, 165, 233] : (Math.random() > 0.5 ? [139, 92, 246] : [226, 232, 240]);
    }
    update(dtFactor) {
      this.twinklePhase += this.twinkleSpeed * dtFactor;

      // 引力黑洞交互：当鼠标按下且开启交互
      if (isInteractive && mouse.isDown && mouse.x > 0 && mouse.y > 0) {
        const dx = mouse.x - this.x;
        const dy = mouse.y - this.y;
        const distSq = dx * dx + dy * dy;

        if (distSq < 270400 && distSq > 100) { // dist between 10 and 520
          const dist = Math.sqrt(distSq);
          const gravityForce = Math.min(28, 480 / dist);
          const nx = dx / dist;
          const ny = dy / dist;
          const spinForce = gravityForce * 0.65;
          const tx = -ny;
          const ty = nx;

          this.vx += (nx * gravityForce + tx * spinForce) * (0.045 * dtFactor);
          this.vy += (ny * gravityForce + ty * spinForce) * (0.045 * dtFactor);
          this.size = Math.min(this.baseSize * 2.2, 5.0);
        }
      } else if (isInteractive) {
        this.size = this.baseSize;
        const dx = mouse.x - this.x;
        const dy = mouse.y - this.y;
        const distSq = dx * dx + dy * dy;
        if (distSq < 19600 && distSq > 0) { // dist < 140
          const dist = Math.sqrt(distSq);
          const repel = (140 - dist) / 140;
          this.vx -= (dx / dist) * repel * (0.6 * dtFactor);
          this.vy -= (dy / dist) * repel * (0.6 * dtFactor);
        }
      } else {
        this.size = this.baseSize;
      }

      this.vx *= 0.985;
      this.vy *= 0.985;
      this.x += this.vx * dtFactor;
      this.y += this.vy * dtFactor;

      if (this.x < 0) this.x = width;
      if (this.x > width) this.x = 0;
      if (this.y < 0) this.y = height;
      if (this.y > height) this.y = 0;
    }
    draw() {
      const alpha = Math.sin(this.twinklePhase) * 0.35 + 0.65;

      // 柔和外光晕（通过透明同心圆绘制，替代高开销的 CPU shadowBlur）
      if (this.isHot || this.size > 2.3) {
        ctx.beginPath();
        ctx.arc(this.x, this.y, this.size * 2.4, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(' + this.rgb.join(',') + ',' + (alpha * 0.22) + ')';
        ctx.fill();
      }

      // 核心星辰点
      ctx.beginPath();
      ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(' + this.rgb.join(',') + ',' + alpha + ')';
      ctx.fill();
    }
  }

  // 4. 画布视口缩放与初始化
  function resize() {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;

    if (cosmicStars.length === 0) {
      for (let i = 0; i < COSMIC_STAR_COUNT; i++) {
        cosmicStars.push(new CosmicStar());
      }
    }
  }
  window.addEventListener('resize', resize);
  resize();

  // 5. 鼠标事件监听
  window.addEventListener('mousemove', function (e) {
    if (!isInteractive) return;
    mouse.x = e.clientX;
    mouse.y = e.clientY;
    mouse.hasMoved = true;
  });

  window.addEventListener('mousedown', function (e) {
    if (!isInteractive) return;
    mouse.isDown = true;
    mouse.downStartTime = Date.now();
    mouse.holdDuration = 0;
  });

  window.addEventListener('mouseup', function (e) {
    if (!isInteractive) {
      mouse.isDown = false;
      return;
    }
    if (mouse.isDown) {
      const holdTime = Date.now() - mouse.downStartTime;
      if (holdTime > 220) {
        const blastPower = Math.min(1.8, holdTime / 600);
        addRipple(mouse.x, mouse.y, 450 * blastPower, 'rgba(139, 92, 246,', 10);
        addRipple(mouse.x, mouse.y, 320 * blastPower, 'rgba(14, 165, 233,', 14);
        spawnSparks(mouse.x, mouse.y, Math.floor(40 * blastPower), [139, 92, 246]);
        spawnSparks(mouse.x, mouse.y, Math.floor(30 * blastPower), [14, 165, 233]);

        const blastRadiusSq = 460 * 460;
        cosmicStars.forEach(function (s) {
          const dx = s.x - mouse.x;
          const dy = s.y - mouse.y;
          const distSq = dx * dx + dy * dy;
          if (distSq < blastRadiusSq) {
            const dist = Math.sqrt(distSq) || 1;
            const force = (460 - dist) / 460 * 18 * blastPower;
            s.vx += (dx / dist) * force;
            s.vy += (dy / dist) * force;
          }
        });
      } else {
        addRipple(mouse.x, mouse.y, 220, 'rgba(14, 165, 233,', 7);
        spawnSparks(mouse.x, mouse.y, 16, [14, 165, 233]);
      }
    }
    mouse.isDown = false;
    mouse.holdDuration = 0;
  });

  window.addEventListener('mouseleave', function () {
    mouse.x = -1000;
    mouse.y = -1000;
    mouse.isDown = false;
  });

  // 6. 主渲染循环管线
  function animate(currentTimestamp) {
    const elapsedMs = currentTimestamp ? (currentTimestamp - lastTimestamp) : 16;
    lastTimestamp = currentTimestamp || performance.now();
    const dtFactor = Math.min(elapsedMs / 16.666, 2.5); // 归一化至 60FPS

    time += 0.01 * dtFactor;
    ctx.clearRect(0, 0, width, height);

    // 深空宇宙渐变底色
    const bgGrad = ctx.createLinearGradient(0, 0, width, height);
    bgGrad.addColorStop(0, '#04060a');
    bgGrad.addColorStop(0.55, '#080d18');
    bgGrad.addColorStop(1, '#030508');
    ctx.fillStyle = bgGrad;
    ctx.fillRect(0, 0, width, height);

    // 随机产生流星
    if (Math.random() < 0.012) {
      spawnMeteor();
    }

    // 绘制流星
    for (let i = meteors.length - 1; i >= 0; i--) {
      const m = meteors[i];
      m.x += m.dx * dtFactor;
      m.y += m.dy * dtFactor;
      m.alpha -= m.decay * dtFactor;

      if (m.alpha <= 0 || m.x < 0 || m.x > width || m.y > height) {
        meteors.splice(i, 1);
        continue;
      }

      ctx.save();
      const grad = ctx.createLinearGradient(m.x, m.y, m.x - m.dx * 5, m.y - m.dy * 5);
      grad.addColorStop(0, 'rgba(255, 255, 255, ' + m.alpha + ')');
      grad.addColorStop(0.3, 'rgba(14, 165, 233, ' + (m.alpha * 0.7) + ')');
      grad.addColorStop(1, 'rgba(139, 92, 246, 0)');
      ctx.strokeStyle = grad;
      ctx.lineWidth = 2.0;
      ctx.beginPath();
      ctx.moveTo(m.x, m.y);
      ctx.lineTo(m.x - m.dx * 5, m.y - m.dy * 5);
      ctx.stroke();
      ctx.restore();
    }

    // 更新与绘制星辰
    for (let i = 0; i < cosmicStars.length; i++) {
      cosmicStars[i].update(dtFactor);
      cosmicStars[i].draw();
    }

    // 批量绘制临近星尘微光连线（单次 beginPath / stroke，性能提升 50 倍）
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(14, 165, 233, 0.12)';
    ctx.lineWidth = 0.65;
    const maxLinkDistSq = 95 * 95;
    for (let i = 0; i < cosmicStars.length; i++) {
      const s1 = cosmicStars[i];
      for (let j = i + 1; j < cosmicStars.length; j++) {
        const s2 = cosmicStars[j];
        const dx = s1.x - s2.x;
        const dy = s1.y - s2.y;
        if (dx * dx + dy * dy < maxLinkDistSq) {
          ctx.moveTo(s1.x, s1.y);
          ctx.lineTo(s2.x, s2.y);
        }
      }
    }
    ctx.stroke();

    // 渲染通用冲击波与水波涟漪
    renderRipplesAndSparks(dtFactor);

    // 鼠标长按黑洞吸积盘特显
    if (isInteractive && mouse.isDown && mouse.x > 0 && mouse.y > 0) {
      renderSingularityAccretion();
    }

    requestAnimationFrame(animate);
  }

  // 渲染函数：黑洞引力坍缩与吸积盘
  function renderSingularityAccretion() {
    const holdTime = Date.now() - mouse.downStartTime;
    const holdProgress = Math.min(1.0, holdTime / 600);
    const pulse = Math.sin(time * 18) * 4;
    const coreR = 14 + holdProgress * 10 + pulse;

    ctx.save();
    const ringAngle = time * 12;
    ctx.translate(mouse.x, mouse.y);
    ctx.rotate(ringAngle);

    // 外圈能量辉光（纯径向渐变，无需 Gaussian blur）
    const glowGrad = ctx.createRadialGradient(0, 0, coreR * 0.6, 0, 0, coreR * 3.5);
    glowGrad.addColorStop(0, 'rgba(' + singularityRgb.join(',') + ', 0.85)');
    glowGrad.addColorStop(0.5, 'rgba(14, 165, 233, 0.45)');
    glowGrad.addColorStop(1, 'rgba(14, 165, 233, 0)');
    ctx.fillStyle = glowGrad;
    ctx.beginPath();
    ctx.arc(0, 0, coreR * 3.5, 0, Math.PI * 2);
    ctx.fill();

    // 椭圆吸积流带
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.75)';
    ctx.lineWidth = 2.0;
    ctx.beginPath();
    ctx.ellipse(0, 0, coreR * 2.2, coreR * 1.1, Math.PI / 4, 0, Math.PI * 2);
    ctx.stroke();

    // 核心引力奇点（深黑吸收核）
    ctx.beginPath();
    ctx.arc(0, 0, coreR, 0, Math.PI * 2);
    ctx.fillStyle = '#020305';
    ctx.fill();

    ctx.restore();
  }

  // 渲染函数：通用波纹与溅射粒子
  function renderRipplesAndSparks(dtFactor) {
    for (let i = ripples.length - 1; i >= 0; i--) {
      const rip = ripples[i];
      rip.r += rip.speed * dtFactor;
      rip.alpha -= 0.016 * dtFactor;

      if (rip.alpha <= 0 || rip.r >= rip.maxR) {
        ripples.splice(i, 1);
        continue;
      }

      ctx.save();
      ctx.beginPath();
      ctx.arc(rip.x, rip.y, rip.r, 0, Math.PI * 2);
      ctx.strokeStyle = rip.color + rip.alpha + ')';
      ctx.lineWidth = Math.max(1, 2.8 * rip.alpha);
      ctx.stroke();
      ctx.restore();
    }

    for (let i = sparkParticles.length - 1; i >= 0; i--) {
      const p = sparkParticles[i];
      p.x += p.vx * dtFactor;
      p.y += p.vy * dtFactor;
      p.vx *= 0.94;
      p.vy *= 0.94;
      p.alpha -= p.decay * dtFactor;

      if (p.alpha <= 0) {
        sparkParticles.splice(i, 1);
        continue;
      }

      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(' + p.rgb.join(',') + ',' + p.alpha + ')';
      ctx.fill();
    }
  }

  // 7. 接收宿主消息响应
  window.addEventListener('message', function (e) {
    const data = e.data || {};
    if (data.type === 'wallpaper_interactive_response' || data.type === 'wallpaper_interactive_changed') {
      if (data.ok && typeof data.interactive === 'boolean') {
        isInteractive = data.interactive;
        if (!isInteractive) {
          mouse.x = -1000;
          mouse.y = -1000;
          mouse.isDown = false;
        }
      }
    } else if (data.type === 'wallpaper_property_change' || data.type === 'wallpaper_property') {
      const key = data.key || data.property;
      handleWallpaperProperty(key, data.value);
    }
  });

  if (window.LinDesk && window.LinDesk.wallpaper && typeof window.LinDesk.wallpaper.onPropertyChange === 'function') {
    window.LinDesk.wallpaper.onPropertyChange(function (prop) {
      if (prop && prop.key) {
        handleWallpaperProperty(prop.key, prop.value);
      }
    });
  }

  // 启动初始化
  sendMessage('wallpaper_get_interactive', {});
  requestAnimationFrame(animate);

})();
