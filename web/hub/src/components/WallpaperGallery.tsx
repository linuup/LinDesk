import React, { useRef, useState, useEffect } from 'react';
import { Play, Square, Sliders, Sparkles, Check, ImageOff } from 'lucide-react';
import { WallpaperInfo } from '../types';

interface WallpaperGalleryProps {
  wallpapers: WallpaperInfo[];
  activeWallpaperId: string;
  onApplyWallpaper: (id: string, packagePath?: string) => void;
  onStopWallpaper: () => void;
  onOpenStudio: (id: string) => void;
}

// Helper to determine the wallpaper's folder name
const getWallpaperFolder = (preset: WallpaperInfo): string => {
  if (preset.packagePath) {
    const clean = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '');
    const folder = clean.split('/').pop();
    if (folder) return folder;
  }
  const id = (preset.id || '').toLowerCase();
  
  if (id.includes('living')) return 'living_wallpaper';
  if (id.includes('neon')) return 'neon_universe';
  return id.replace(/^wallpaper:/i, '').replace(/^com\.lindesk\.wallpaper\./i, '');
};

// Helper to resolve the relative preview URL to the real wallpaper's index.html
const getWallpaperPreviewUrl = (preset: WallpaperInfo): string => {
  const folder = getWallpaperFolder(preset);
  return `../wallpapers/${folder}/index.html`;
};

// Real Live Wallpaper Thumbnail Preview with Standard Desktop Aspect Ratio Scaling
const WallpaperThumbnail: React.FC<{ wallpaper: WallpaperInfo; isActive: boolean }> = ({ wallpaper, isActive }) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [scale, setScale] = useState(0.25);
  const previewUrl = getWallpaperPreviewUrl(wallpaper);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const update = () => {
      if (el.clientWidth > 0) {
        setScale(el.clientWidth / 1280);
      }
    };
    update();
    const observer = new ResizeObserver(update);
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  return (
    <div ref={containerRef} className="relative w-full aspect-video bg-black overflow-hidden select-none">
      <div
        style={{
          width: '1280px',
          height: '720px',
          transform: `scale(${scale})`,
          transformOrigin: 'top left',
        }}
        className="pointer-events-none select-none"
      >
        <iframe
          src={previewUrl}
          title={wallpaper.name}
          className="w-full h-full border-0 pointer-events-none select-none"
          tabIndex={-1}
          scrolling="no"
        />
      </div>

      {/* Active Pill Overlay */}
      {isActive && (
        <div className="absolute top-2.5 left-2.5">
          <span className="flex items-center gap-1.5 px-2.5 py-1 rounded-full bg-black/60 backdrop-blur-md border border-emerald-500/40 text-emerald-300 text-[11px] font-semibold shadow-xs">
            <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse" />
            桌面正在使用
          </span>
        </div>
      )}
    </div>
  );
};

// Hero Live Preview Window
const HeroPreviewWindow: React.FC<{ currentWall: WallpaperInfo; isRunning: boolean }> = ({ currentWall, isRunning }) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [scale, setScale] = useState(0.3);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const update = () => {
      if (el.clientWidth > 0) {
        setScale(el.clientWidth / 1280);
      }
    };
    update();
    const observer = new ResizeObserver(update);
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  return (
    <div ref={containerRef} className="relative w-full lg:w-96 aspect-video rounded-xl overflow-hidden border border-slate-200 shadow-sm bg-black shrink-0 select-none">
      <div
        style={{
          width: '1280px',
          height: '720px',
          transform: `scale(${scale})`,
          transformOrigin: 'top left',
        }}
        className="pointer-events-none select-none"
      >
        <iframe
          src={getWallpaperPreviewUrl(currentWall)}
          title={currentWall.name}
          className="w-full h-full border-0 pointer-events-none select-none"
          tabIndex={-1}
          scrolling="no"
        />
      </div>
      <div className="absolute top-2.5 left-2.5 flex items-center gap-1.5 px-2.5 py-1 rounded-full bg-black/60 backdrop-blur-md border border-white/20 text-xs">
        <span className={`w-2 h-2 rounded-full ${isRunning ? 'bg-emerald-400 animate-pulse' : 'bg-slate-400'}`} />
        <span className={`font-medium ${isRunning ? 'text-emerald-300' : 'text-slate-300'}`}>
          {isRunning ? '当前桌面正在使用' : '未应用 (Windows 默认壁纸)'}
        </span>
      </div>
    </div>
  );
};

export const WallpaperGallery: React.FC<WallpaperGalleryProps> = ({
  wallpapers,
  activeWallpaperId,
  onApplyWallpaper,
  onStopWallpaper,
  onOpenStudio,
}) => {
  // Strict exact matching helper
  const isWallpaperActive = (preset: WallpaperInfo): boolean => {
    if (!activeWallpaperId) return false;
    const normActive = activeWallpaperId.replace(/^wallpaper:/i, '').trim().toLowerCase();
    const normPresetId = (preset.id || '').replace(/^wallpaper:/i, '').trim().toLowerCase();
    if (normActive === normPresetId) return true;

    if (preset.packagePath) {
      const folder = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '').split('/').pop()?.toLowerCase();
      if (folder && (normActive === folder || normActive === `wallpaper:${folder}`)) {
        return true;
      }
    }
    return false;
  };

  const activeWall = wallpapers.find(p => isWallpaperActive(p)) || null;
  const isRunning = !!activeWallpaperId && !!activeWall;
  const currentWall = activeWall || (wallpapers.length > 0 ? wallpapers[0] : null);

  return (
    <div className="space-y-6 animate-in fade-in duration-150">
      {/* 1. Active Wallpaper Showcase Hero (Using Real Wallpaper Live Preview) */}
      {currentWall ? (
        <section className="relative overflow-hidden rounded-2xl border border-slate-200/90 bg-white p-5 md:p-6 shadow-2xs">
          <div className="flex flex-col lg:flex-row items-center gap-6">
            {/* Real Wallpaper Live Preview Window */}
            <HeroPreviewWindow currentWall={currentWall} isRunning={isRunning} />

            {/* Active Info & Controls */}
            <div className="flex-1 space-y-3 w-full">
              <div className="flex items-center gap-2">
                <span className="px-2.5 py-0.5 rounded-md text-[11px] font-semibold bg-indigo-50 text-indigo-700 border border-indigo-200/80">
                  当前活动壁纸
                </span>
                <span className="text-xs font-mono text-slate-500">
                  {currentWall.name}
                </span>
              </div>

              <h2 className="text-lg md:text-xl font-bold text-slate-900 tracking-tight">
                {currentWall.name}
              </h2>
              <p className="text-xs text-slate-600 leading-relaxed max-w-2xl">
                {currentWall.description || '支持动态交互与微粒跟随渲染。'}
              </p>

              <div className="flex flex-wrap items-center gap-2.5 pt-2 border-t border-slate-100">
                {isRunning ? (
                  <button
                    onClick={onStopWallpaper}
                    className="px-4 py-2 rounded-xl bg-rose-50 hover:bg-rose-100 text-rose-700 border border-rose-200 text-xs font-semibold flex items-center gap-2 transition-all active:scale-95 shadow-2xs"
                  >
                    <Square className="w-3.5 h-3.5 fill-rose-600 text-rose-600" />
                    关闭壁纸
                  </button>
                ) : (
                  <button
                    onClick={() => onApplyWallpaper(currentWall.id, currentWall.packagePath)}
                    className="px-4 py-2 rounded-xl bg-indigo-600 hover:bg-indigo-700 text-white text-xs font-semibold shadow-2xs flex items-center gap-2 transition-all active:scale-95"
                  >
                    <Play className="w-3.5 h-3.5 fill-white" />
                    设为桌面壁纸
                  </button>
                )}

                <button
                  onClick={() => onOpenStudio(currentWall.id)}
                  className="px-3.5 py-2 rounded-xl bg-white hover:bg-slate-50 text-slate-700 border border-slate-200 text-xs font-medium flex items-center gap-2 shadow-2xs transition-all"
                >
                  <Sliders className="w-3.5 h-3.5 text-indigo-600" />
                  进入工坊调参...
                </button>
              </div>
            </div>
          </div>
        </section>
      ) : null}

      {/* 2. Gallery Presets Grid */}
      <div>
        <div className="flex items-center justify-between mb-3.5">
          <div className="flex items-center gap-2">
            <Sparkles className="w-4 h-4 text-indigo-600" />
            <h3 className="text-sm font-bold text-slate-900">动态壁纸库</h3>
          </div>
        </div>

        {wallpapers.length === 0 ? (
          <div className="p-12 text-center rounded-2xl border border-dashed border-slate-200 bg-white/60 space-y-2">
            <ImageOff className="w-8 h-8 text-slate-400 mx-auto" />
            <p className="text-sm text-slate-700 font-semibold">未检测到已安装的动态壁纸</p>
            <p className="text-xs text-slate-400">请在 resources/wallpapers 目录下放置有效的动态壁纸包工程</p>
          </div>
        ) : (
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
            {wallpapers.map((preset) => {
              const isActive = isWallpaperActive(preset);

              return (
                <div
                  key={preset.id}
                  className={`rounded-2xl border transition-all duration-200 bg-white flex flex-col justify-between overflow-hidden ${
                    isActive
                      ? 'border-indigo-400 ring-1 ring-indigo-500/20 shadow-xs'
                      : 'border-slate-200/90 shadow-2xs hover:border-slate-300 hover:shadow-xs'
                  }`}
                >
                  {/* Real Live Wallpaper Preview Thumbnail with 1280x720 scaling */}
                  <WallpaperThumbnail wallpaper={preset} isActive={isActive} />

                  {/* Card Body */}
                  <div className="p-4.5 space-y-2.5 flex-1 flex flex-col justify-between">
                    <div className="space-y-1.5">
                      <div className="flex items-center justify-between">
                        <span className="text-xs font-bold text-slate-900 tracking-tight line-clamp-1">
                          {preset.name}
                        </span>
                        <span className="px-1.5 py-0.5 rounded text-[10px] font-mono bg-slate-100 text-slate-600 border border-slate-200">
                          v{preset.version || '1.0.0'}
                        </span>
                      </div>

                      <p className="text-xs text-slate-500 line-clamp-2 leading-relaxed">
                        {preset.description || '支持动态交互与微粒跟随渲染。'}
                      </p>
                    </div>
                  </div>

                  {/* Card Action Footer */}
                  <div className="px-4.5 py-3 bg-slate-50/70 border-t border-slate-100 flex items-center justify-between text-xs">
                    <button
                      onClick={() => onOpenStudio(preset.id)}
                      className="px-2.5 py-1.5 rounded-lg bg-white hover:bg-slate-100 text-slate-700 border border-slate-200 text-xs font-semibold flex items-center gap-1.5 shadow-2xs transition-all"
                    >
                      <Sliders className="w-3.5 h-3.5 text-indigo-600" />
                      工坊调参
                    </button>

                    <button
                      onClick={() => onApplyWallpaper(preset.id, preset.packagePath)}
                      className={`px-3.5 py-1.5 rounded-lg text-xs font-semibold transition-all flex items-center gap-1.5 active:scale-95 ${
                        isActive
                          ? 'bg-emerald-50 text-emerald-700 border border-emerald-200 cursor-default shadow-2xs'
                          : 'bg-indigo-600 hover:bg-indigo-700 text-white shadow-2xs'
                      }`}
                    >
                      {isActive ? (
                        <>
                          <Check className="w-3.5 h-3.5 text-emerald-600" />
                          正在使用
                        </>
                      ) : (
                        <>
                          <Play className="w-3.5 h-3.5 fill-white" />
                          设为壁纸
                        </>
                      )}
                    </button>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
};
