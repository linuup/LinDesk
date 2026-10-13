(function () {
  'use strict';

  // 1. LinWidget Bridge 通信层
  function sendMessage(type, payload) {
    const msg = Object.assign({ type: type }, payload || {});
    // 原生 CEF OSR fetch 拦截通道
    fetch('http://linwidget.bridge/postMessage', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(msg)
    }).catch(function () {
      // 降级使用 window.parent.postMessage
      try {
        window.parent.postMessage(msg, '*');
      } catch (e) {}
    });
  }

  const canvas = document.getElementById('bg-canvas');
  const ctx = canvas.getContext('2d');
  const video = document.getElementById('bg-video');
  const statusText = document.getElementById('status-text');
  const btnSelect = document.getElementById('btn-select');
  const btnReset = document.getElementById('btn-reset');

  let width = 0;
  let height = 0;
  let animationId = null;
  let isVideoPlaying = false;
  let mouse = { x: -1000, y: -1000, radius: 160 };

  function resize() {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;
  }
  window.addEventListener('resize', resize);
  resize();

  window.addEventListener('mousemove', function (e) {
    mouse.x = e.clientX;
    mouse.y = e.clientY;
  });

  // 2. 粒子与极光仿真系统
  const particles = [];
  const particleCount = 75;

  class Particle {
    constructor() {
      this.reset();
    }
    reset() {
      this.x = Math.random() * width;
      this.y = Math.random() * height;
      this.vx = (Math.random() - 0.5) * 0.8;
      this.vy = (Math.random() - 0.5) * 0.8;
      this.size = Math.random() * 2.5 + 1.2;
      this.alpha = Math.random() * 0.6 + 0.2;
      this.color = Math.random() > 0.4 ? 'rgba(0, 180, 216, ' : 'rgba(114, 9, 183, ';
    }
    update() {
      this.x += this.vx;
      this.y += this.vy;

      if (this.x < 0) this.x = width;
      if (this.x > width) this.x = 0;
      if (this.y < 0) this.y = height;
      if (this.y > height) this.y = 0;

      // 鼠标交互推开
      const dx = mouse.x - this.x;
      const dy = mouse.y - this.y;
      const dist = Math.sqrt(dx * dx + dy * dy);
      if (dist < mouse.radius) {
        const force = (mouse.radius - dist) / mouse.radius;
        this.x -= (dx / dist) * force * 3.5;
        this.y -= (dy / dist) * force * 3.5;
      }
    }
    draw() {
      ctx.beginPath();
      ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
      ctx.fillStyle = this.color + this.alpha + ')';
      ctx.shadowBlur = 8;
      ctx.shadowColor = '#00b4d8';
      ctx.fill();
    }
  }

  for (let i = 0; i < particleCount; i++) {
    particles.push(new Particle());
  }

  let time = 0;
  function animate() {
    time += 0.006;
    ctx.clearRect(0, 0, width, height);

    // 绘制深邃背景渐变
    const bgGrad = ctx.createLinearGradient(0, 0, width, height);
    bgGrad.addColorStop(0, '#0a0d14');
    bgGrad.addColorStop(0.5, '#0e1726');
    bgGrad.addColorStop(1, '#05070b');
    ctx.fillStyle = bgGrad;
    ctx.fillRect(0, 0, width, height);

    // 绘制柔和动态极光流动带
    for (let j = 0; j < 3; j++) {
      ctx.save();
      ctx.beginPath();
      const waveY = height * (0.35 + j * 0.18);
      ctx.moveTo(0, height);
      ctx.lineTo(0, waveY);

      for (let x = 0; x <= width; x += 40) {
        const wave = Math.sin(x * 0.002 + time * 1.5 + j * 1.2) * 55 +
                     Math.cos(x * 0.004 - time + j) * 35;
        ctx.lineTo(x, waveY + wave);
      }
      ctx.lineTo(width, height);
      ctx.closePath();

      const auroraGrad = ctx.createLinearGradient(0, waveY - 50, 0, height);
      if (j === 0) {
        auroraGrad.addColorStop(0, 'rgba(0, 180, 216, 0.18)');
        auroraGrad.addColorStop(0.5, 'rgba(72, 12, 168, 0.08)');
        auroraGrad.addColorStop(1, 'rgba(10, 13, 20, 0)');
      } else if (j === 1) {
        auroraGrad.addColorStop(0, 'rgba(114, 9, 183, 0.14)');
        auroraGrad.addColorStop(0.6, 'rgba(0, 119, 182, 0.06)');
        auroraGrad.addColorStop(1, 'rgba(10, 13, 20, 0)');
      } else {
        auroraGrad.addColorStop(0, 'rgba(67, 97, 238, 0.12)');
        auroraGrad.addColorStop(1, 'rgba(10, 13, 20, 0)');
      }
      ctx.fillStyle = auroraGrad;
      ctx.fill();
      ctx.restore();
    }

    // 更新与连接粒子连线
    for (let i = 0; i < particles.length; i++) {
      particles[i].update();
      particles[i].draw();

      for (let k = i + 1; k < particles.length; k++) {
        const dx = particles[i].x - particles[k].x;
        const dy = particles[i].y - particles[k].y;
        const dist = Math.sqrt(dx * dx + dy * dy);
        if (dist < 110) {
          ctx.beginPath();
          ctx.moveTo(particles[i].x, particles[i].y);
          ctx.lineTo(particles[k].x, particles[k].y);
          ctx.strokeStyle = 'rgba(0, 180, 216, ' + (0.16 * (1 - dist / 110)) + ')';
          ctx.lineWidth = 0.8;
          ctx.stroke();
        }
      }
    }

    if (!isVideoPlaying) {
      animationId = requestAnimationFrame(animate);
    }
  }

  animate();

  // 3. 媒体源设置与响应
  function applyMediaSource(sourcePath) {
    if (!sourcePath || sourcePath.trim() === '') {
      isVideoPlaying = false;
      video.pause();
      video.classList.remove('active');
      video.src = '';
      statusText.textContent = 'Aurora Particles Active';
      if (!animationId) {
        animate();
      }
      return;
    }

    const ext = sourcePath.split('.').pop().toLowerCase();
    const isVideo = ['mp4', 'webm', 'ogg'].indexOf(ext) !== -1;
    const mediaUrl = 'linwidget://wallpaper:lindesk.wallpaper/_media?path=' + encodeURIComponent(sourcePath);

    if (isVideo) {
      video.src = mediaUrl;
      video.play().then(function () {
        isVideoPlaying = true;
        video.classList.add('active');
        statusText.textContent = sourcePath.split(/[\\/]/).pop();
        if (animationId) {
          cancelAnimationFrame(animationId);
          animationId = null;
        }
      }).catch(function (err) {
        console.warn('Video play error:', err);
        statusText.textContent = 'Play Error: ' + err.message;
      });
    } else {
      // 图片直接作为视频海报背景或覆盖画布
      video.pause();
      video.classList.remove('active');
      isVideoPlaying = false;
      const img = new Image();
      img.onload = function () {
        ctx.drawImage(img, 0, 0, width, height);
        statusText.textContent = sourcePath.split(/[\\/]/).pop();
      };
      img.src = mediaUrl;
    }
  }

  // 4. 监听 C++ 主机消息响应
  window.addEventListener('message', function (event) {
    const data = event.data;
    if (!data || typeof data !== 'object') return;

    if (data.type === 'wallpaper_source_response' || data.type === 'wallpaper_source_changed') {
      if (data.ok && data.sourcePath) {
        applyMediaSource(data.sourcePath);
      } else if (data.type === 'wallpaper_source_changed' && !data.ok) {
        if (data.error) {
          console.log('Wallpaper select cancelled or failed:', data.error);
        }
      }
    }
  });

  // UI 按钮绑定
  btnSelect.addEventListener('click', function () {
    sendMessage('wallpaper_set_source', { dialog: true });
  });

  btnReset.addEventListener('click', function () {
    sendMessage('wallpaper_set_source', { filePath: '' });
    applyMediaSource('');
  });

  // 初始化时请求已持久化的壁纸配置
  sendMessage('wallpaper_get_source', {});
})();
