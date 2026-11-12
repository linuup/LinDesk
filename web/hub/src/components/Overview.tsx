import React, { useState, useEffect, useMemo, useCallback } from 'react';
import { 
  Play, Check, Sliders, Sparkles, Film, 
  Search, ArrowRight, Monitor, Power, Plus, ChevronLeft, ChevronRight,
  Layers, Eye, StickyNote, UploadCloud, Zap, HelpCircle
} from 'lucide-react';
import { WallpaperInfo, WidgetInfo } from '../types';
import { VirtualWallpaperViewport } from './VirtualWallpaperViewport';
import { VideoHoverPreview, formatVideoUrl } from './VideoHoverPreview';
import { VideoLiveCanvas } from './VideoLiveCanvas';

interface OverviewProps {
  wallpapers: WallpaperInfo[];
  widgets: WidgetInfo[];
  activeWallpaperId: string;
  onApplyWallpaper: (id: string, packagePath?: string) => void;
  onStopWallpaper: () => void;
  onGoToStudio: (wallpaperId?: string) => void;
  onGoToWidgets: () => void;
  onImportMedia: () => void;
  onLaunchWidget: (id: string, packagePath: string) => void;
  onStopWidget: (id: string) => void;
  onCreateWidget?: () => void;
}

type CategoryType = 'all' | 'video' | 'interactive' | 'cyberpunk' | 'nature' | 'minimal';

// Helper to determine the wallpaper's folder name
const getWallpaperFolder = (preset: WallpaperInfo): string => {
  if (preset.packagePath) {
    const clean = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '');
    const folder = clean.split('/').pop();
    if (folder) return folder;
  }
  const id = (preset.id || '').toLowerCase();
  if (id === 'lindesk.wallpaper') return 'living_wallpaper';
  const lastPart = id.split('.').pop();
  if (lastPart) return lastPart;
  return id.replace(/^wallpaper:/i, '');
};

const getWallpaperHtmlUrl = (preset: WallpaperInfo): string | null => {
  if (preset.wallpaperType === 'video' || preset.wallpaperType === 'image') return null;
  if (preset.packagePath) {
    const clean = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '');
    const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
    return `${prefix}${clean}/index.html`;
  }
  const folder = getWallpaperFolder(preset);
  return `../wallpapers/${folder}/index.html`;
};

// Fast static thumbnail resolver
const getWallpaperThumbnail = (preset: WallpaperInfo): { type: 'image' | 'fallback'; url?: string } => {
  if (preset.previewUrl) {
    return { type: 'image', url: preset.previewUrl };
  }
  if (preset.sourcePath && preset.wallpaperType === 'image') {
    const clean = preset.sourcePath.replace(/\\/g, '/');
    const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
    return { type: 'image', url: `${prefix}${clean}` };
  }
  if (preset.packagePath) {
    const clean = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '');
    const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
    return { type: 'image', url: `${prefix}${clean}/preview.png` };
  }
  if (preset.wallpaperType === 'image' && preset.mediaUrl) {
    return { type: 'image', url: preset.mediaUrl };
  }
  if (preset.wallpaperType === 'video') {
    const raw = preset.mediaUrl || preset.sourcePath || preset.packagePath;
    if (raw) {
      const clean = raw.replace(/\\/g, '/').replace(/^file:\/\/\/?/i, '');
      return {
        type: 'image',
        url: `http://linwidget.bridge/video-frame?path=${encodeURIComponent(clean)}`
      };
    }
  }
  return { type: 'fallback' };
};

const CardThumbnail: React.FC<{ wall: WallpaperInfo; className?: string; isHovered?: boolean }> = ({ 
  wall, 
  className = '',
  isHovered = false
}) => {
  const [imgError, setImgError] = useState(false);
  const thumb = getWallpaperThumbnail(wall);
  const isVideo = wall.wallpaperType === 'video';

  if (isVideo) {
    const raw = wall.mediaUrl || wall.sourcePath || wall.packagePath || '';
    const videoUrl = formatVideoUrl(raw);
    return (
      <VideoHoverPreview
        videoUrl={videoUrl}
        posterUrl={thumb.type === 'image' ? thumb.url : undefined}
        isHovered={isHovered}
        className={className}
      />
    );
  }

  if (thumb.type === 'image' && thumb.url && !imgError) {
    return (
      <div className={`relative w-full h-full overflow-hidden select-none ${className}`}>
        <img
          src={thumb.url}
          alt={wall.name}
          onError={() => setImgError(true)}
          className="w-full h-full object-cover transition-transform duration-500 ease-out group-hover:scale-106 select-none"
          loading="lazy"
        />
        {/* Physical display screen bezel & subtle contrast gradient */}
        <div className="absolute inset-0 ring-1 ring-inset ring-white/10 dark:ring-white/5 pointer-events-none" />
        <div className="absolute inset-0 bg-gradient-to-t from-black/45 via-transparent to-transparent pointer-events-none" />
      </div>
    );
  }

  return (
    <div className={`w-full h-full bg-gradient-to-br from-slate-100 via-indigo-50/50 to-slate-200 dark:from-slate-950 dark:via-slate-900 dark:to-indigo-950 flex flex-col items-center justify-center gap-2 p-3 text-center select-none ${className}`}>
      <div className="w-10 h-10 rounded-xl bg-white/80 dark:bg-white/10 backdrop-blur-xs flex items-center justify-center text-indigo-600 dark:text-indigo-400 border border-slate-200/80 dark:border-white/10 shadow-xs">
        {isVideo ? <Film className="w-5 h-5" /> : <Sparkles className="w-5 h-5" />}
      </div>
      <span className="text-xs font-semibold text-slate-700 dark:text-slate-200 line-clamp-1">{wall.name}</span>
    </div>
  );
};

const OverviewWidgetIcon: React.FC<{ widget: WidgetInfo }> = ({ widget }) => {
  const [imgError, setImgError] = useState(false);
  const iconUrl = useMemo(() => {
    if (!widget.packagePath) return null;
    const clean = widget.packagePath.replace(/\\/g, '/');
    const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
    return `${prefix}${clean}/icon.png`;
  }, [widget.packagePath]);

  if (iconUrl && !imgError) {
    return (
      <div className="w-9 h-9 rounded-xl overflow-hidden bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shadow-2xs flex items-center justify-center p-1.5 shrink-0">
        <img
          src={iconUrl}
          alt={widget.name}
          onError={() => setImgError(true)}
          className="w-full h-full object-contain select-none"
        />
      </div>
    );
  }

  return (
    <div className="w-9 h-9 rounded-xl bg-indigo-50 dark:bg-white/10 text-indigo-600 dark:text-indigo-400 border border-indigo-100 dark:border-white/10 flex items-center justify-center shrink-0">
      <Layers className="w-4 h-4" />
    </div>
  );
};

interface EmptyOverviewGuideProps {
  onImportMedia: () => void;
  onGoToStudio: () => void;
  onGoToWidgets: () => void;
  onCreateWidget?: () => void;
}

const EmptyOverviewGuide: React.FC<EmptyOverviewGuideProps> = ({
  onImportMedia,
  onGoToStudio,
  onGoToWidgets,
  onCreateWidget
}) => {
  return (
    <div className="space-y-8 pb-12 animate-fade-in select-none">
      {/* 1. 顶部沉浸欢迎卡片 (Hero Welcome Banner) */}
      <section className="relative rounded-3xl overflow-hidden bg-gradient-to-br from-indigo-900 via-slate-900 to-slate-950 text-white p-8 md:p-12 border border-indigo-500/20 shadow-xl dark:shadow-2xl">
        {/* 背景微光氛围装饰 */}
        <div className="absolute -right-24 -top-24 w-96 h-96 bg-indigo-500/20 rounded-full blur-3xl pointer-events-none" />
        <div className="absolute right-1/4 -bottom-24 w-80 h-80 bg-violet-500/15 rounded-full blur-3xl pointer-events-none" />

        <div className="relative z-10 max-w-2xl">
          <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-white/10 backdrop-blur-md border border-white/15 text-indigo-200 text-xs font-medium mb-4">
            <Sparkles className="w-3.5 h-3.5 text-indigo-300 animate-pulse" />
            <span>开启你的个性化桌面体验</span>
          </div>

          <h1 className="text-3xl sm:text-4xl font-extrabold text-white tracking-tight leading-tight">
            欢迎使用桌面工坊
          </h1>

          <p className="mt-3 text-sm sm:text-base text-slate-300 leading-relaxed">
            目前桌面还没有添加任何壁纸和小工具。只需简单两步，就能把喜欢的动态视频设为桌面背景，摆放轻量桌面小工具。
          </p>

          <div className="mt-6 flex flex-wrap items-center gap-3">
            <button
              type="button"
              onClick={onImportMedia}
              className="px-5 py-2.5 rounded-xl bg-indigo-500 hover:bg-indigo-400 text-white text-xs sm:text-sm font-semibold flex items-center gap-2 shadow-lg shadow-indigo-500/30 transition-all duration-200 hover:scale-[1.02] active:scale-95 cursor-pointer"
            >
              <Plus className="w-4 h-4" />
              <span>添加第一张壁纸</span>
            </button>

            <button
              type="button"
              onClick={onGoToStudio}
              className="px-5 py-2.5 rounded-xl bg-white/10 hover:bg-white/20 text-white text-xs sm:text-sm font-semibold border border-white/20 backdrop-blur-md flex items-center gap-2 transition-all duration-200 cursor-pointer"
            >
              <Sliders className="w-4 h-4 text-indigo-300" />
              <span>进入壁纸工坊</span>
            </button>

            <button
              type="button"
              onClick={onGoToWidgets}
              className="px-5 py-2.5 rounded-xl bg-white/5 hover:bg-white/15 text-slate-200 hover:text-white text-xs sm:text-sm font-medium border border-white/10 transition-all duration-200 cursor-pointer"
            >
              <Layers className="w-4 h-4 text-slate-300" />
              <span>浏览桌面小工具</span>
            </button>
          </div>
        </div>
      </section>

      {/* 2. 引导核心卡片 (Step by Step Guide) */}
      <section className="grid grid-cols-1 md:grid-cols-2 gap-5">
        {/* 指引 1：壁纸 */}
        <div className="bg-white/95 dark:bg-[#131927]/90 rounded-2xl p-6 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md flex flex-col justify-between transition-colors">
          <div>
            <div className="w-12 h-12 rounded-xl bg-indigo-50 dark:bg-indigo-950/60 text-indigo-600 dark:text-indigo-400 border border-indigo-100 dark:border-indigo-800/50 flex items-center justify-center mb-4 shadow-2xs">
              <Film className="w-6 h-6" />
            </div>

            <div className="flex items-center gap-2">
              <span className="text-[11px] font-bold px-2 py-0.5 rounded-md bg-indigo-100/70 dark:bg-indigo-900/40 text-indigo-700 dark:text-indigo-300">
                第一步
              </span>
              <h3 className="text-base font-bold text-slate-900 dark:text-slate-100">
                添置动态壁纸
              </h3>
            </div>

            <p className="mt-2 text-xs text-slate-600 dark:text-slate-400 leading-relaxed">
              支持把喜欢的 MP4 视频、风景照片或 GIF 动图一键设为桌面壁纸。画面在桌面上平滑循环，工作和放松时更加赏心悦目。
            </p>

            {/* 小贴士 */}
            <div className="mt-4 p-3 rounded-xl bg-slate-50 dark:bg-slate-900/70 border border-slate-200/70 dark:border-slate-800/60 text-[11px] text-slate-500 dark:text-slate-400 flex items-start gap-2">
              <span className="text-amber-500">💡</span>
              <span>可以直接把电脑里的视频或图片文件拖进这个窗口，松开鼠标即可自动添加。</span>
            </div>
          </div>

          <div className="mt-6 pt-4 border-t border-slate-100 dark:border-slate-800/80 flex items-center justify-between gap-3">
            <button
              type="button"
              onClick={onImportMedia}
              className="px-4 py-2 rounded-xl bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold flex items-center gap-1.5 shadow-xs transition cursor-pointer"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>选择视频或照片</span>
            </button>

            <button
              type="button"
              onClick={onGoToStudio}
              className="text-xs text-indigo-600 dark:text-indigo-400 hover:underline font-medium flex items-center gap-1 cursor-pointer"
            >
              <span>去壁纸工坊</span>
              <ArrowRight className="w-3 h-3" />
            </button>
          </div>
        </div>

        {/* 指引 2：小工具 */}
        <div className="bg-white/95 dark:bg-[#131927]/90 rounded-2xl p-6 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md flex flex-col justify-between transition-colors">
          <div>
            <div className="w-12 h-12 rounded-xl bg-amber-50 dark:bg-amber-950/60 text-amber-600 dark:text-amber-400 border border-amber-100 dark:border-amber-800/50 flex items-center justify-center mb-4 shadow-2xs">
              <StickyNote className="w-6 h-6" />
            </div>

            <div className="flex items-center gap-2">
              <span className="text-[11px] font-bold px-2 py-0.5 rounded-md bg-amber-100/70 dark:bg-amber-900/40 text-amber-700 dark:text-amber-300">
                第二步
              </span>
              <h3 className="text-base font-bold text-slate-900 dark:text-slate-100">
                摆上桌面小工具
              </h3>
            </div>

            <p className="mt-2 text-xs text-slate-600 dark:text-slate-400 leading-relaxed">
              丰富实用的桌面小工具摆在桌面上随手可用，不用每次打开应用，桌面就是你的高效工作台。
            </p>

            {/* 小贴士 */}
            <div className="mt-4 p-3 rounded-xl bg-slate-50 dark:bg-slate-900/70 border border-slate-200/70 dark:border-slate-800/60 text-[11px] text-slate-500 dark:text-slate-400 flex items-start gap-2">
              <span className="text-amber-500">💡</span>
              <span>小工具摆到桌面上之后，鼠标按住就能自由拖动位置，想摆哪就摆哪。</span>
            </div>
          </div>

          <div className="mt-6 pt-4 border-t border-slate-100 dark:border-slate-800/80 flex items-center justify-between gap-3">
            <button
              type="button"
              onClick={onGoToWidgets}
              className="px-4 py-2 rounded-xl bg-slate-900 dark:bg-white text-white dark:text-slate-900 hover:bg-slate-800 dark:hover:bg-slate-100 text-xs font-semibold flex items-center gap-1.5 shadow-xs transition cursor-pointer"
            >
              <Layers className="w-3.5 h-3.5" />
              <span>进入小工具中心</span>
            </button>

            {onCreateWidget && (
              <button
                type="button"
                onClick={onCreateWidget}
                className="text-xs text-indigo-600 dark:text-indigo-400 hover:underline font-medium flex items-center gap-1 cursor-pointer"
              >
                <span>添加第一个小工具</span>
                <ArrowRight className="w-3 h-3" />
              </button>
            )}
          </div>
        </div>
      </section>

      {/* 3. 拖拽快速添加热区 (Drop Target Zone) */}
      <section
        onClick={onImportMedia}
        className="group rounded-2xl border-2 border-dashed border-indigo-200 dark:border-indigo-900/60 hover:border-indigo-500 bg-indigo-50/40 dark:bg-indigo-950/20 hover:bg-indigo-50/80 dark:hover:bg-indigo-950/40 p-8 text-center transition-all duration-200 cursor-pointer"
      >
        <div className="w-12 h-12 rounded-full bg-white dark:bg-slate-800 text-indigo-600 dark:text-indigo-400 shadow-sm flex items-center justify-center mx-auto mb-3 group-hover:scale-110 transition-transform">
          <UploadCloud className="w-6 h-6" />
        </div>
        <h4 className="text-sm font-bold text-slate-900 dark:text-slate-100">
          点击选择或将视频、图片直接拖到这里
        </h4>
        <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
          支持常见 MP4 视频、PNG、JPG 或 GIF 动图，立即生成桌面壁纸
        </p>
      </section>

      {/* 4. 特色保障与贴心常识 (3 Features Strip) */}
      <section className="grid grid-cols-1 sm:grid-cols-3 gap-4">
        <div className="bg-white/95 dark:bg-[#131927]/90 rounded-xl p-4 border border-slate-200/80 dark:border-slate-800/80 flex items-start gap-3">
          <div className="w-9 h-9 rounded-lg bg-emerald-50 dark:bg-emerald-950/60 text-emerald-600 dark:text-emerald-400 flex items-center justify-center shrink-0">
            <Zap className="w-4 h-4" />
          </div>
          <div>
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-100">全屏游戏自动暂停</h5>
            <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400 leading-normal">
              玩全屏游戏或打开全屏软件时，动态画面会自动静默，电脑完全不卡顿。
            </p>
          </div>
        </div>

        <div className="bg-white/95 dark:bg-[#131927]/90 rounded-xl p-4 border border-slate-200/80 dark:border-slate-800/80 flex items-start gap-3">
          <div className="w-9 h-9 rounded-lg bg-blue-50 dark:bg-blue-950/60 text-blue-600 dark:text-blue-400 flex items-center justify-center shrink-0">
            <Monitor className="w-4 h-4" />
          </div>
          <div>
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-100">不遮挡桌面文件</h5>
            <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400 leading-normal">
              桌面图标与快捷方式始终正常点击使用，小工具可以随意调整摆放。
            </p>
          </div>
        </div>

        <div className="bg-white/95 dark:bg-[#131927]/90 rounded-xl p-4 border border-slate-200/80 dark:border-slate-800/80 flex items-start gap-3">
          <div className="w-9 h-9 rounded-lg bg-purple-50 dark:bg-purple-950/60 text-purple-600 dark:text-purple-400 flex items-center justify-center shrink-0">
            <Power className="w-4 h-4" />
          </div>
          <div>
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-100">随时一键恢复</h5>
            <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400 leading-normal">
              想换回原来的系统桌面，只需点击关闭壁纸，瞬间还原原本模样。
            </p>
          </div>
        </div>
      </section>

      {/* 5. 常见疑问解答 (FAQ) */}
      <section className="bg-white/95 dark:bg-[#131927]/90 rounded-2xl p-6 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md">
        <div className="flex items-center gap-2 mb-4">
          <HelpCircle className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />
          <h4 className="text-xs font-bold text-slate-900 dark:text-slate-100 uppercase tracking-wider">
            新手贴心问答
          </h4>
        </div>

        <div className="space-y-3">
          <div className="p-3.5 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/60 dark:border-slate-800/60">
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200 flex items-center gap-1.5">
              <span>Q：添加动态壁纸会很耗电或导致电脑变卡吗？</span>
            </h5>
            <p className="mt-1.5 text-xs text-slate-600 dark:text-slate-400 leading-relaxed pl-5">
              不会。LinDesk 内置了智能节能技术：当你打开全屏游戏、全屏播放视频或遮挡桌面时，动态壁纸会自动暂停运行，达到 0 额外负担；笔记本使用电池时也会自动降低耗电。
            </p>
          </div>

          <div className="p-3.5 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/60 dark:border-slate-800/60">
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200 flex items-center gap-1.5">
              <span>Q：小工具摆在桌面上怎么移动和关闭？</span>
            </h5>
            <p className="mt-1.5 text-xs text-slate-600 dark:text-slate-400 leading-relaxed pl-5">
              在桌面上直接用鼠标左键拖拽小工具的标题栏即可移动位置，拖动边角即可缩放大小。如果你想收起某个小工具，在“桌面小工具”页面点击“停止”即可随收随开。
            </p>
          </div>

          <div className="p-3.5 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/60 dark:border-slate-800/60">
            <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200 flex items-center gap-1.5">
              <span>Q：如何添加更多我喜欢的视频或照片？</span>
            </h5>
            <p className="mt-1.5 text-xs text-slate-600 dark:text-slate-400 leading-relaxed pl-5">
              点击上方的“添加第一张壁纸”按钮，或者把任何你喜欢的 MP4 视频、高品质动漫短片或生活照片直接拖进这个窗口，就会自动导入并即刻在桌面上播放。
            </p>
          </div>
        </div>
      </section>
    </div>
  );
};

export const Overview: React.FC<OverviewProps> = ({
  wallpapers,
  widgets,
  activeWallpaperId,
  onApplyWallpaper,
  onStopWallpaper,
  onGoToStudio,
  onGoToWidgets,
  onImportMedia,
  onLaunchWidget,
  onStopWidget,
  onCreateWidget
}) => {
  if (wallpapers.length === 0 && widgets.length === 0) {
    return (
      <EmptyOverviewGuide
        onImportMedia={onImportMedia}
        onGoToStudio={() => onGoToStudio()}
        onGoToWidgets={onGoToWidgets}
        onCreateWidget={onCreateWidget}
      />
    );
  }

  const [searchQuery, setSearchQuery] = useState('');
  const [selectedCategory, setSelectedCategory] = useState<CategoryType>('all');
  const [currentHeroIndex, setCurrentHeroIndex] = useState(0);
  const [selectedWallpaperId, setSelectedWallpaperId] = useState<string | null>(null);
  const [isHeroHovered, setIsHeroHovered] = useState(false);
  const [hoveredCardId, setHoveredCardId] = useState<string | null>(null);

  // Check if wallpaper is currently running
  const isWallpaperRunning = (wall: WallpaperInfo): boolean => {
    if (!activeWallpaperId) return false;
    const normActive = activeWallpaperId.replace(/^wallpaper:/i, '').trim().toLowerCase();
    const normPresetId = (wall.id || '').replace(/^wallpaper:/i, '').trim().toLowerCase();
    if (!normActive || !normPresetId) return false;
    if (normActive === normPresetId) return true;

    const folder = getWallpaperFolder(wall).toLowerCase();
    if (folder && normActive === folder) return true;

    const normActiveLast = normActive.split('.').pop() || '';
    if (folder && normActiveLast !== 'wallpaper' && normActiveLast === folder) return true;

    return false;
  };

  const activeWall = useMemo(() => {
    return wallpapers.find(w => isWallpaperRunning(w)) || null;
  }, [wallpapers, activeWallpaperId]);

  // Featured Hero Wallpapers (Includes user-selected wallpaper at position 0)
  const heroWallpapers = useMemo(() => {
    if (wallpapers.length === 0) return [];
    if (selectedWallpaperId) {
      const selected = wallpapers.find(w => w.id === selectedWallpaperId);
      if (selected) {
        const others = wallpapers.filter(w => w.id !== selectedWallpaperId);
        return [selected, ...others.slice(0, 4)];
      }
    }
    return wallpapers.slice(0, 5);
  }, [wallpapers, selectedWallpaperId]);

  const handlePrevHero = useCallback(() => {
    if (heroWallpapers.length <= 1) return;
    setCurrentHeroIndex(prev => (prev === 0 ? heroWallpapers.length - 1 : prev - 1));
  }, [heroWallpapers.length]);

  const handleNextHero = useCallback(() => {
    if (heroWallpapers.length <= 1) return;
    setCurrentHeroIndex(prev => (prev + 1) % heroWallpapers.length);
  }, [heroWallpapers.length]);

  // Auto-cycle hero banner (pauses when hovered)
  useEffect(() => {
    if (heroWallpapers.length <= 1 || isHeroHovered) return;
    const timer = setInterval(() => {
      handleNextHero();
    }, 6000);
    return () => clearInterval(timer);
  }, [heroWallpapers.length, isHeroHovered, handleNextHero]);

  const currentHero = heroWallpapers[currentHeroIndex] || null;
  const isCurrentHeroActive = currentHero ? isWallpaperRunning(currentHero) : false;

  // Filter wallpapers by category and search
  const filteredWallpapers = useMemo(() => {
    return wallpapers.filter(wall => {
      const matchSearch = 
        !searchQuery || 
        wall.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
        (wall.description && wall.description.toLowerCase().includes(searchQuery.toLowerCase()));

      if (!matchSearch) return false;

      if (selectedCategory === 'all') return true;
      if (selectedCategory === 'video') return wall.wallpaperType === 'video';
      if (selectedCategory === 'interactive') return Boolean(wall.interactive) || (wall.wallpaperType !== 'video' && wall.wallpaperType !== 'image');
      
      const tagLower = (wall.tag || '').toLowerCase();
      const nameLower = wall.name.toLowerCase();
      const descLower = (wall.description || '').toLowerCase();

      if (selectedCategory === 'cyberpunk') {
        return tagLower.includes('赛博') || tagLower.includes('neon') || nameLower.includes('neon') || nameLower.includes('赛博') || descLower.includes('赛博');
      }
      if (selectedCategory === 'nature') {
        return tagLower.includes('自然') || tagLower.includes('风景') || nameLower.includes('风景') || nameLower.includes('雨') || nameLower.includes('雨夜') || descLower.includes('自然');
      }
      if (selectedCategory === 'minimal') {
        return tagLower.includes('极简') || tagLower.includes('抽象') || nameLower.includes('极简') || descLower.includes('极简');
      }

      return true;
    });
  }, [wallpapers, searchQuery, selectedCategory]);

  return (
    <div className="space-y-8 pb-12 animate-fade-in">
      {/* 1. Silky Smooth Cinematic Hero Showcase Section */}
      <section 
        className="relative rounded-2xl overflow-hidden bg-slate-950 border border-slate-200/90 dark:border-slate-800/90 shadow-lg dark:shadow-2xl group/hero select-none"
        onMouseEnter={() => setIsHeroHovered(true)}
        onMouseLeave={() => setIsHeroHovered(false)}
      >
        {currentHero ? (
          <div className="relative h-[320px] sm:h-[350px] md:h-[380px] w-full flex items-end p-6 md:p-10 overflow-hidden">
            {/* Silky Smooth Pre-rendered Crossfade Slides Stack */}
            <div className="absolute inset-0 z-0 overflow-hidden">
              {heroWallpapers.map((wall, idx) => {
                const isActive = idx === currentHeroIndex;
                const htmlUrl = getWallpaperHtmlUrl(wall);
                return (
                  <div
                    key={wall.id || idx}
                    className={`absolute inset-0 transition-all duration-700 ease-in-out ${
                      isActive 
                        ? 'opacity-100 scale-100 z-10' 
                        : 'opacity-0 scale-105 pointer-events-none z-0'
                    }`}
                  >
                    {isActive && htmlUrl ? (
                      <VirtualWallpaperViewport
                        wall={wall}
                        src={htmlUrl}
                        className="w-full h-full"
                      />
                    ) : wall.wallpaperType === 'video' ? (
                      <VideoLiveCanvas
                        videoPath={wall.mediaUrl || wall.sourcePath || wall.packagePath || ''}
                        posterUrl={wall.previewUrl}
                        isActive={isActive}
                        fps={30}
                        objectFit="cover"
                        className="w-full h-full"
                      />
                    ) : (
                      <CardThumbnail 
                        wall={wall} 
                        className="w-full h-full object-cover" 
                      />
                    )}
                    {/* Dark gradient vignettes for contrast and depth */}
                    <div className="absolute inset-0 bg-gradient-to-t from-slate-950/95 via-slate-950/40 to-transparent" />
                    <div className="absolute inset-0 bg-gradient-to-r from-slate-950/80 via-slate-950/20 to-transparent" />
                  </div>
                );
              })}
            </div>

            {/* Bottom Content: Wallpaper Name + Single "应用到桌面" Button (Left), Clean Dots (Right) */}
            <div className="relative z-20 w-full flex flex-col sm:flex-row sm:items-end justify-between gap-4">
              {/* Left: Wallpaper Name + "应用到桌面" CTA */}
              <div className="flex flex-col items-start gap-4 max-w-xl">
                <h1 className="text-2xl sm:text-3xl md:text-4xl font-extrabold text-white tracking-tight leading-tight drop-shadow-lg transition-all duration-300">
                  {currentHero.name}
                </h1>

                <button
                  type="button"
                  onClick={() => onApplyWallpaper(currentHero.id, currentHero.packagePath)}
                  className={`px-6 py-3 rounded-xl text-xs font-bold flex items-center gap-2 transition-all duration-200 shadow-xl cursor-pointer ${
                    isCurrentHeroActive
                      ? 'bg-emerald-600 hover:bg-emerald-500 text-white shadow-emerald-500/25 ring-2 ring-emerald-400/40'
                      : 'bg-indigo-600 hover:bg-indigo-500 text-white shadow-indigo-500/30 hover:scale-[1.02] active:scale-95'
                  }`}
                >
                  {isCurrentHeroActive ? (
                    <>
                      <Check className="w-4 h-4 stroke-[3]" />
                      <span>正在使用中</span>
                    </>
                  ) : (
                    <>
                      <Play className="w-4 h-4 fill-white" />
                      <span>应用到桌面</span>
                    </>
                  )}
                </button>
              </div>

              {/* Right: Clean Sleek Indicator Dots */}
              {heroWallpapers.length > 1 && (
                <div className="flex items-center gap-2 bg-black/40 backdrop-blur-md px-3.5 py-2 rounded-full border border-white/10 self-start sm:self-end">
                  {heroWallpapers.map((_, idx) => {
                    const isSelected = idx === currentHeroIndex;
                    return (
                      <button
                        key={idx}
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          setCurrentHeroIndex(idx);
                        }}
                        className={`rounded-full transition-all duration-300 cursor-pointer ${
                          isSelected
                            ? 'w-7 h-2 bg-white shadow-xs'
                            : 'w-2 h-2 bg-white/40 hover:bg-white/70'
                        }`}
                        title={`第 ${idx + 1} 张壁纸`}
                      />
                    );
                  })}
                </div>
              )}
            </div>

            {/* Big Prominent Left & Right Lateral Chevron Buttons (Easy to Hit) */}
            {heroWallpapers.length > 1 && (
              <>
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation();
                    handlePrevHero();
                  }}
                  className="absolute left-3.5 top-1/2 -translate-y-1/2 z-30 w-12 h-12 rounded-full bg-slate-950/60 hover:bg-indigo-600 text-white/90 hover:text-white border border-white/20 hover:border-white/40 shadow-2xl backdrop-blur-md flex items-center justify-center transition-all duration-200 hover:scale-110 active:scale-95 cursor-pointer"
                  title="上一款壁纸"
                >
                  <ChevronLeft className="w-6 h-6 stroke-[2.5]" />
                </button>

                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation();
                    handleNextHero();
                  }}
                  className="absolute right-3.5 top-1/2 -translate-y-1/2 z-30 w-12 h-12 rounded-full bg-slate-950/60 hover:bg-indigo-600 text-white/90 hover:text-white border border-white/20 hover:border-white/40 shadow-2xl backdrop-blur-md flex items-center justify-center transition-all duration-200 hover:scale-110 active:scale-95 cursor-pointer"
                  title="下一款壁纸"
                >
                  <ChevronRight className="w-6 h-6 stroke-[2.5]" />
                </button>
              </>
            )}
          </div>
        ) : (
          <div className="h-[280px] flex flex-col items-center justify-center text-center p-6 text-slate-300">
            <div className="w-12 h-12 rounded-2xl bg-indigo-500/20 border border-indigo-400/20 text-indigo-400 flex items-center justify-center mb-3 shadow-lg">
              <Film className="w-6 h-6" />
            </div>
            <h3 className="text-lg font-bold text-white">添加你的第一张动态壁纸</h3>
            <p className="text-xs text-slate-300 mt-1.5 max-w-md leading-relaxed">
              支持把喜欢的 MP4 视频、GIF 动图或高清照片设为桌面背景。可以直接将文件拖拽到此窗口，或者点击下方按钮。
            </p>
            <div className="mt-4 flex items-center gap-3">
              <button
                type="button"
                onClick={onImportMedia}
                className="px-5 py-2.5 bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold rounded-xl flex items-center gap-1.5 shadow-md transition cursor-pointer"
              >
                <Plus className="w-4 h-4" />
                <span>选择视频或照片</span>
              </button>
              <button
                type="button"
                onClick={() => onGoToStudio()}
                className="px-4 py-2.5 bg-white/10 hover:bg-white/20 text-white text-xs font-medium rounded-xl border border-white/15 transition cursor-pointer"
              >
                <span>前往壁纸工坊</span>
              </button>
            </div>
          </div>
        )}
      </section>

      {/* 2. Active Desktop Status Bar (活跃桌面视窗) */}
      <section className="bg-white/95 dark:bg-[#131927]/90 rounded-xl p-4 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 backdrop-blur-md transition-colors">
        <div className="flex items-center gap-3.5">
          <div className="w-12 h-12 rounded-lg overflow-hidden bg-slate-100 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shrink-0 relative shadow-2xs">
            {activeWall ? (
              <CardThumbnail wall={activeWall} className="w-full h-full" />
            ) : (
              <div className="w-full h-full flex items-center justify-center text-slate-400 dark:text-slate-500">
                <Monitor className="w-5 h-5" />
              </div>
            )}
          </div>

          <div>
            <div className="flex items-center gap-2">
              <span className="text-xs font-semibold text-slate-900 dark:text-slate-100">
                {activeWall ? activeWall.name : '系统原生静态壁纸'}
              </span>
              <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-medium ${
                activeWall 
                  ? 'bg-emerald-50 dark:bg-emerald-950/80 text-emerald-700 dark:text-emerald-300 border border-emerald-200 dark:border-emerald-800/60' 
                  : 'bg-slate-100 dark:bg-slate-800/80 text-slate-600 dark:text-slate-400 border border-slate-200 dark:border-slate-700/50'
              }`}>
                <span className={`w-1.5 h-1.5 rounded-full ${activeWall ? 'bg-emerald-500 animate-pulse' : 'bg-slate-400 dark:bg-slate-500'}`} />
                {activeWall ? '动态壁纸运行中' : '未加载动态壁纸'}
              </span>
            </div>
            <p className="text-[11px] text-slate-500 dark:text-slate-400 mt-0.5">
              {activeWall 
                ? `${activeWall.author || '本地壁纸'} · 支持全屏游戏智能挂起与自动节能` 
                : '从下方精选壁纸中挑选一款，一键赋予桌面生动体验'}
            </p>
          </div>
        </div>

        {/* Quick controls for active wallpaper */}
        <div className="flex items-center gap-2 self-end sm:self-center shrink-0">
          {activeWall ? (
            <>
              <button
                type="button"
                onClick={() => onGoToStudio(activeWall.id)}
                className="px-3 py-1.5 rounded-lg text-xs font-medium text-slate-700 dark:text-slate-200 bg-white dark:bg-slate-800 hover:bg-slate-50 dark:hover:bg-slate-700 border border-slate-200 dark:border-slate-700/60 transition-colors flex items-center gap-1.5 shadow-2xs cursor-pointer"
              >
                <Sliders className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                <span>工坊调参</span>
              </button>

              <button
                type="button"
                onClick={onStopWallpaper}
                className="px-3 py-1.5 rounded-lg text-xs font-medium text-rose-600 dark:text-rose-400 bg-rose-50/80 dark:bg-rose-950/40 hover:bg-rose-100/80 dark:hover:bg-rose-900/40 border border-rose-200 dark:border-rose-800/50 transition-colors flex items-center gap-1.5 shadow-2xs cursor-pointer"
                title="停用动态壁纸并还原默认桌面"
              >
                <Power className="w-3.5 h-3.5" />
                <span>关闭壁纸</span>
              </button>
            </>
          ) : (
            <button
              type="button"
              onClick={() => onGoToStudio()}
              className="px-3 py-1.5 rounded-lg text-xs font-medium text-indigo-600 dark:text-indigo-300 bg-indigo-50/80 dark:bg-indigo-950/40 hover:bg-indigo-100/80 dark:hover:bg-indigo-900/40 border border-indigo-200 dark:border-indigo-800/60 transition-colors flex items-center gap-1.5 shadow-2xs cursor-pointer"
            >
              <Sparkles className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
              <span>打开壁纸工坊</span>
            </button>
          )}
        </div>
      </section>

      {/* 3. Filter Navigation & Search Header */}
      <section className="space-y-4">
        <div className="flex flex-col md:flex-row md:items-center justify-between gap-3">
          {/* Category Tabs */}
          <div className="flex items-center gap-1.5 overflow-x-auto pb-1 md:pb-0 scrollbar-none">
            {[
              { id: 'all', label: '全部精选' },
              { id: 'video', label: '动态视频' },
              { id: 'interactive', label: '交互与场景' },
              { id: 'cyberpunk', label: '赛博与霓虹' },
              { id: 'nature', label: '自然与风景' },
              { id: 'minimal', label: '极简意境' },
            ].map(cat => (
              <button
                key={cat.id}
                type="button"
                onClick={() => setSelectedCategory(cat.id as CategoryType)}
                className={`px-3.5 py-1.5 rounded-lg text-xs font-medium transition-all duration-150 whitespace-nowrap cursor-pointer ${
                  selectedCategory === cat.id
                    ? 'bg-indigo-600 text-white shadow-xs font-semibold'
                    : 'bg-white dark:bg-slate-900/80 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                {cat.label}
              </button>
            ))}
          </div>

          {/* Search Box & Quick Import */}
          <div className="flex items-center gap-2">
            <div className="relative flex-1 md:w-60">
              <Search className="w-3.5 h-3.5 absolute left-3 top-1/2 -translate-y-1/2 text-slate-400 dark:text-slate-500" />
              <input
                type="text"
                placeholder="搜索壁纸..."
                value={searchQuery}
                onChange={e => setSearchQuery(e.target.value)}
                className="w-full pl-8 pr-3 py-1.5 bg-white dark:bg-slate-900/90 border border-slate-200 dark:border-slate-800 rounded-lg text-xs text-slate-800 dark:text-slate-100 placeholder-slate-400 dark:placeholder-slate-500 focus:outline-hidden focus:border-indigo-500 focus:ring-2 focus:ring-indigo-500/20 shadow-2xs transition"
              />
            </div>

            <button
              type="button"
              onClick={onImportMedia}
              className="px-3 py-1.5 rounded-lg text-xs font-medium text-slate-700 dark:text-slate-200 bg-white dark:bg-slate-900 hover:bg-slate-50 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800 shadow-2xs flex items-center gap-1.5 cursor-pointer shrink-0 transition"
              title="导入本地视频或图像"
            >
              <Plus className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
              <span>导入</span>
            </button>
          </div>
        </div>

        {/* 4. Curated Wallpapers Grid */}
        <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-4">
          {filteredWallpapers.map(wall => {
            const isRunning = isWallpaperRunning(wall);
            const isSelectedInHero = currentHero?.id === wall.id;
            return (
              <div
                key={wall.id}
                onClick={() => {
                  setSelectedWallpaperId(wall.id);
                  setCurrentHeroIndex(0);
                }}
                className={`group relative bg-white dark:bg-[#131927]/90 rounded-xl border transition-all duration-200 overflow-hidden flex flex-col backdrop-blur-xs cursor-pointer ${
                  isSelectedInHero
                    ? 'ring-2 ring-indigo-500 shadow-md border-indigo-500/60 dark:border-indigo-400/60 scale-[1.01]'
                    : 'border-slate-200/90 dark:border-slate-800/90 shadow-xs hover:shadow-md hover:border-slate-300 dark:hover:border-slate-700/90'
                }`}
              >
                {/* 16:9 Thumbnail Box */}
                <div 
                  className="relative aspect-video w-full bg-slate-950 overflow-hidden"
                  onMouseEnter={() => setHoveredCardId(wall.id)}
                  onMouseLeave={() => setHoveredCardId(null)}
                >
                  <CardThumbnail wall={wall} isHovered={hoveredCardId === wall.id} />

                  {/* Active Indicator Badge */}
                  {isRunning ? (
                    <div className="absolute top-2.5 left-2.5 z-10 px-2 py-0.5 rounded-md bg-emerald-600/90 text-white text-[10px] font-semibold flex items-center gap-1 backdrop-blur-xs shadow-xs">
                      <Check className="w-3 h-3 stroke-[2.5]" />
                      <span>正在使用</span>
                    </div>
                  ) : isSelectedInHero ? (
                    <div className="absolute top-2.5 left-2.5 z-10 px-2 py-0.5 rounded-md bg-indigo-600/90 text-white text-[10px] font-semibold flex items-center gap-1 backdrop-blur-xs shadow-xs">
                      <Eye className="w-3 h-3" />
                      <span>正在预览</span>
                    </div>
                  ) : null}

                  {/* Tag Badges: Type (图片/视频/场景) + Separate Interactive Tag (可交互) */}
                  <div className="absolute top-2.5 right-2.5 z-10 flex items-center gap-1.5">
                    {/* 1. Base Media Type Tag */}
                    <span className="px-2 py-0.5 rounded-md bg-black/60 text-slate-200 text-[10px] font-medium backdrop-blur-xs border border-white/10 shadow-xs">
                      {wall.wallpaperType === 'image' ? '图片' : wall.wallpaperType === 'video' ? '视频' : '场景'}
                    </span>

                    {/* 2. Capability: Separate 可交互 Tag */}
                    {Boolean(wall.interactive) && (
                      <span className="px-1.5 py-0.5 rounded-md bg-indigo-500/80 text-white text-[10px] font-medium backdrop-blur-xs border border-indigo-400/30 shadow-xs flex items-center gap-1">
                        <Sparkles className="w-2.5 h-2.5 text-indigo-200" />
                        <span>可交互</span>
                      </span>
                    )}
                  </div>

                  {/* Hover Quick Action Overlay */}
                  <div className="absolute inset-0 bg-slate-950/70 opacity-0 group-hover:opacity-100 transition-opacity duration-200 flex items-center justify-center gap-2 p-3 backdrop-blur-[2px]">
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation();
                        onApplyWallpaper(wall.id, wall.packagePath);
                      }}
                      className={`px-3.5 py-2 rounded-lg text-xs font-semibold flex items-center gap-1.5 shadow-md cursor-pointer transition transform active:scale-95 ${
                        isRunning 
                          ? 'bg-emerald-600 text-white hover:bg-emerald-500' 
                          : 'bg-indigo-600 text-white hover:bg-indigo-500'
                      }`}
                    >
                      <Play className="w-3.5 h-3.5 fill-current" />
                      <span>{isRunning ? '刷新应用' : '应用到桌面'}</span>
                    </button>

                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation();
                        onGoToStudio(wall.id);
                      }}
                      className="p-2 rounded-lg bg-white/20 hover:bg-white/30 text-white backdrop-blur-sm transition cursor-pointer"
                      title="在工坊中微调参数"
                    >
                      <Sliders className="w-4 h-4" />
                    </button>
                  </div>
                </div>

                {/* Card Meta Info */}
                <div className="p-3 flex items-center justify-between gap-2">
                  <div className="min-w-0 flex-1">
                    <h4 className="text-xs font-semibold text-slate-900 dark:text-slate-100 truncate" title={wall.name}>
                      {wall.name}
                    </h4>
                    <p className="text-[11px] text-slate-500 dark:text-slate-400 truncate mt-0.5">
                      {wall.author || 'LinDesk'}
                    </p>
                  </div>
                </div>
              </div>
            );
          })}

          {/* Quick Import Card */}
          <div
            onClick={onImportMedia}
            className="group relative aspect-video sm:aspect-auto sm:min-h-[160px] rounded-xl border-2 border-dashed border-slate-300 dark:border-slate-800 hover:border-indigo-500/80 bg-white/70 dark:bg-slate-900/30 hover:bg-indigo-50/50 dark:hover:bg-indigo-950/25 transition-all duration-200 flex flex-col items-center justify-center p-4 text-center cursor-pointer select-none shadow-2xs"
          >
            <div className="w-9 h-9 rounded-full bg-slate-100 dark:bg-slate-800 group-hover:bg-indigo-600 text-slate-600 dark:text-slate-300 group-hover:text-white flex items-center justify-center shadow-xs mb-2 transition-colors">
              <Plus className="w-4 h-4" />
            </div>
            <span className="text-xs font-semibold text-slate-800 dark:text-slate-200 group-hover:text-indigo-600 dark:group-hover:text-indigo-400 transition-colors">
              导入本地壁纸
            </span>
            <span className="text-[10px] text-slate-400 dark:text-slate-500 mt-1">
              支持 MP4 视频、PNG、JPG 或 GIF
            </span>
          </div>
        </div>

        {filteredWallpapers.length === 0 && (
          <div className="text-center py-12 bg-white dark:bg-[#131927]/90 rounded-xl border border-slate-200/90 dark:border-slate-800/90 p-6 shadow-xs">
            <Search className="w-8 h-8 text-slate-400 dark:text-slate-500 mx-auto mb-2" />
            <p className="text-xs font-medium text-slate-600 dark:text-slate-400">未找到符合条件的壁纸</p>
            <button
              type="button"
              onClick={() => { setSelectedCategory('all'); setSearchQuery(''); }}
              className="mt-3 text-xs text-indigo-600 dark:text-indigo-400 hover:underline font-medium"
            >
              清除筛选条件
            </button>
          </div>
        )}
      </section>

      {/* 5. Desktop Widgets Quick Access Strip (桌面组件一键快启) */}
      <section className="bg-white dark:bg-gradient-to-r dark:from-slate-900 dark:to-indigo-950 rounded-2xl p-5 md:p-6 text-slate-800 dark:text-white border border-slate-200/90 dark:border-transparent shadow-xs dark:shadow-lg">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-4">
          <div className="flex items-center gap-2.5">
            <div className="w-8 h-8 rounded-lg bg-indigo-50 dark:bg-white/10 flex items-center justify-center text-indigo-600 dark:text-indigo-300 border border-indigo-100 dark:border-white/10 shadow-2xs">
              <Layers className="w-4 h-4" />
            </div>
            <div>
              <h3 className="text-sm font-bold text-slate-900 dark:text-white tracking-tight">桌面小组件库</h3>
              <p className="text-[11px] text-slate-500 dark:text-slate-300">
                可自由拖拽、定位于桌面任何位置的动态轻量小组件
              </p>
            </div>
          </div>

          {widgets.length > 0 && (
            <button
              type="button"
              onClick={onGoToWidgets}
              className="self-start sm:self-center px-3 py-1.5 rounded-lg text-xs font-medium text-slate-700 dark:text-white/90 bg-slate-100 dark:bg-white/10 hover:bg-slate-200 dark:hover:bg-white/20 border border-slate-200 dark:border-white/15 backdrop-blur-xs flex items-center gap-1.5 transition cursor-pointer hover:text-slate-900 dark:hover:text-white"
            >
              <span>管理全部组件</span>
              <ArrowRight className="w-3.5 h-3.5" />
            </button>
          )}
        </div>

        {/* Horizontal Widget Tiles */}
        <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-3">
          {widgets.slice(0, 3).map(widget => (
            <div
              key={widget.id}
              className="bg-slate-50/80 hover:bg-slate-100/90 dark:bg-white/5 dark:hover:bg-white/10 border border-slate-200/80 dark:border-white/10 rounded-xl p-3 flex items-center justify-between gap-3 transition shadow-2xs"
            >
              <div className="flex items-center gap-2.5 min-w-0">
                <OverviewWidgetIcon widget={widget} />
                <div className="min-w-0">
                  <div className="flex items-center gap-1.5">
                    <h5 className="text-xs font-semibold text-slate-900 dark:text-white truncate">{widget.name}</h5>
                    <span className={`w-2 h-2 rounded-full shrink-0 ${widget.running ? 'bg-emerald-500' : 'bg-slate-400'}`} />
                  </div>
                  <p className="text-[11px] text-slate-500 dark:text-slate-400 truncate mt-0.5">
                    {widget.description || widget.author || 'LinDesk 桌面组件'}
                  </p>
                </div>
              </div>

              <button
                type="button"
                onClick={() => {
                  if (widget.running) {
                    onStopWidget(widget.id);
                  } else {
                    onLaunchWidget(widget.id, widget.packagePath);
                  }
                }}
                className={`px-3 py-1 rounded-lg text-[11px] font-semibold transition shrink-0 cursor-pointer ${
                  widget.running
                    ? 'bg-rose-50 dark:bg-rose-500/20 text-rose-600 dark:text-rose-300 hover:bg-rose-100 dark:hover:bg-rose-500/30 border border-rose-200 dark:border-rose-500/30'
                    : 'bg-indigo-600 dark:bg-indigo-500 text-white hover:bg-indigo-500 dark:hover:bg-indigo-400 shadow-xs'
                }`}
              >
                {widget.running ? '停止' : '运行'}
              </button>
            </div>
          ))}

          {widgets.length === 0 && (
            <div className="col-span-full py-6 text-center bg-slate-50/50 dark:bg-white/5 rounded-xl border border-dashed border-slate-200 dark:border-white/10 p-4">
              <Layers className="w-6 h-6 text-indigo-500 mx-auto mb-2 opacity-80" />
              <p className="text-xs font-semibold text-slate-800 dark:text-slate-200">
                尚未添加桌面小工具
              </p>
              <p className="text-[11px] text-slate-500 dark:text-slate-400 mt-0.5">
                添加桌面专属小工具，让桌面成为高效便捷的控制台
              </p>
              <div className="mt-3 flex items-center justify-center gap-2">
                <button
                  type="button"
                  onClick={onGoToWidgets}
                  className="px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-medium rounded-lg shadow-xs transition cursor-pointer"
                >
                  去小工具中心挑选
                </button>
                {onCreateWidget && (
                  <button
                    type="button"
                    onClick={onCreateWidget}
                    className="px-3.5 py-1.5 bg-slate-100 hover:bg-slate-200 dark:bg-white/10 dark:hover:bg-white/20 text-slate-700 dark:text-slate-200 text-xs font-medium rounded-lg border border-slate-200 dark:border-white/10 transition cursor-pointer"
                  >
                    + 新建小工具
                  </button>
                )}
              </div>
            </div>
          )}
        </div>
      </section>
    </div>
  );
};
