import React, { useState, useEffect, useRef } from 'react';
import { 
  Play, Square, Sliders, ImageOff, Search, Plus, 
  Upload, Volume2, VolumeX, Moon, RefreshCw, Eye, CheckCircle2, Sparkles,
  Trash2, FolderOpen, Settings2, Maximize2, Gauge, Film, Image as ImageIcon,
  Monitor, ChevronDown, ChevronRight
} from 'lucide-react';
import { WallpaperInfo, UniversalWallpaperSettings, StagedWallpaperMedia } from '../types';
import { VirtualWallpaperViewport } from './VirtualWallpaperViewport';
import { WallpaperDeleteModal } from './WallpaperDeleteModal';
import { VideoHoverPreview, formatVideoUrl } from './VideoHoverPreview';
import { VideoLiveCanvas } from './VideoLiveCanvas';

interface WallpaperStudioProps {
  wallpapers: WallpaperInfo[];
  activeWallpaperId: string;
  isExternalDragging?: boolean;
  onApplyWallpaper: (id: string, packagePath?: string) => void;
  onStopWallpaper: () => void;
  onSendProperty: (property: string, value: any, widgetId?: string, packagePath?: string) => void;
  onApplyPreset?: () => void;
  onImportMedia?: (name: string) => void;
  onArchiveWallpaper?: (id: string) => void;
  onRelocateWallpaper?: (id: string, filePath?: string) => void;
  onEditCustomWallpaper?: (media: StagedWallpaperMedia) => void;
  onDeleteWallpaper?: (id: string, packagePath: string, name: string) => void;
  onOpenFolder?: (id: string, packagePath: string) => void;
  selectedWallpaperId?: string;
}

type FilterMode = 'all' | 'running' | 'video' | 'interactive' | 'custom';

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

// Helper to resolve the preview URL (supports both built-in relative paths and user-imported absolute paths)
const getWallpaperPreviewUrl = (preset: WallpaperInfo): string => {
  if (preset.packagePath) {
    const clean = preset.packagePath.replace(/\\/g, '/').replace(/\/+$/, '');
    const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
    return `${prefix}${clean}/index.html`;
  }
  const folder = getWallpaperFolder(preset);
  return `../wallpapers/${folder}/index.html`;
};

const isCustomWallpaper = (wall: WallpaperInfo): boolean => {
  if (wall.isCustom) return true;
  const id = (wall.id || '').toLowerCase();
  return id.startsWith('custom.');
};

// Fast static thumbnail resolver to avoid running heavy iframes or video decoders in gallery cards
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

const ThumbnailView: React.FC<{ wall: WallpaperInfo; isHovered?: boolean }> = ({ 
  wall, 
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
      />
    );
  }

  if (thumb.type === 'image' && thumb.url && !imgError) {
    return (
      <div className="relative w-full h-full overflow-hidden select-none">
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
    <div className="w-full h-full bg-gradient-to-br from-slate-100 via-indigo-50/50 to-slate-200 dark:from-slate-900 dark:via-slate-800 dark:to-indigo-950 flex flex-col items-center justify-center gap-2 p-3 text-center select-none">
      <div className="w-9 h-9 rounded-xl bg-white/80 dark:bg-white/10 backdrop-blur-xs flex items-center justify-center text-indigo-600 dark:text-indigo-400 border border-slate-200/80 dark:border-white/10 shadow-xs">
        {isVideo ? <Film className="w-4 h-4" /> : <Sparkles className="w-4 h-4" />}
      </div>
      <span className="text-[11px] font-semibold text-slate-700 dark:text-slate-200 line-clamp-1">{wall.name}</span>
    </div>
  );
};

// 模糊百分比 (0% ~ 100%) 换算为 CSS 高斯模糊像素 (0px ~ 20px)
const blurPercentToPx = (percent: number): number => {
  if (!percent || percent <= 0) return 0;
  const p = percent > 20 ? (percent / 100) * 20 : percent;
  return Math.min(20, Math.max(0, p));
};

export const WallpaperStudio: React.FC<WallpaperStudioProps> = ({
  wallpapers,
  activeWallpaperId,
  isExternalDragging = false,
  onApplyWallpaper,
  onStopWallpaper,
  onSendProperty,
  onApplyPreset,
  onImportMedia,
  onArchiveWallpaper,
  onRelocateWallpaper,
  onEditCustomWallpaper,
  onDeleteWallpaper,
  onOpenFolder,
  selectedWallpaperId
}) => {
  const isWallpaperRunningOnDesktop = (wall: WallpaperInfo): boolean => {
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

  const runningWall = wallpapers.find(w => isWallpaperRunningOnDesktop(w)) || null;

  const [selectedWallId, setSelectedWallId] = useState<string>(() => {
    if (selectedWallpaperId) return selectedWallpaperId;
    if (activeWallpaperId) return activeWallpaperId;
    return wallpapers.length > 0 ? wallpapers[0].id : '';
  });

  useEffect(() => {
    if (selectedWallpaperId) {
      setSelectedWallId(selectedWallpaperId);
    }
  }, [selectedWallpaperId]);

  const [filterMode, setFilterMode] = useState<FilterMode>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [isDraggingFile, setIsDraggingFile] = useState(false);
  const [showDesktopIcons, setShowDesktopIcons] = useState(false);
  const [wallpaperToDelete, setWallpaperToDelete] = useState<WallpaperInfo | null>(null);
  const [hoveredWallId, setHoveredWallId] = useState<string | null>(null);
  const [isHostControlsOpen, setIsHostControlsOpen] = useState(false);

  const [universal, setUniversal] = useState<UniversalWallpaperSettings>({
    volume: 80,
    audioDucking: true,
    dimming: 15,
    blur: 0,
    scaling: 'cover',
    playbackRate: 1.0,
    interactive: true,
    muted: false
  });

  const [customProps, setCustomProps] = useState<Record<string, any>>({});
  const previewIframeRef = useRef<HTMLIFrameElement | null>(null);

  const postPropertyToPreview = (key: string, value: any) => {
    try {
      previewIframeRef.current?.contentWindow?.postMessage(
        { type: 'wallpaper_property_change', key, property: key, value },
        '*'
      );
    } catch {
      // Ignore cross-origin / loading issues
    }
  };

  const syncAllPropsToPreview = () => {
    setTimeout(() => {
      Object.entries(universal).forEach(([k, v]) => postPropertyToPreview(k, v));
      Object.entries(customProps).forEach(([k, v]) => postPropertyToPreview(k, v));
    }, 60);
  };

  useEffect(() => {
    if (activeWallpaperId) {
      setSelectedWallId(activeWallpaperId);
    }
  }, [activeWallpaperId]);

  useEffect(() => {
    if (runningWall && !selectedWallId) {
      setSelectedWallId(runningWall.id);
    } else if (!selectedWallId && wallpapers.length > 0) {
      setSelectedWallId(wallpapers[0].id);
    }
  }, [wallpapers, runningWall, selectedWallId]);

  const selectedWall = wallpapers.find(w => {
    const normSel = selectedWallId.replace(/^wallpaper:/i, '').toLowerCase();
    const normId = (w.id || '').replace(/^wallpaper:/i, '').toLowerCase();
    const folder = getWallpaperFolder(w).toLowerCase();
    return normSel === normId || (folder && normSel === folder);
  }) || runningWall || (wallpapers.length > 0 ? wallpapers[0] : null);

  useEffect(() => {
    if (selectedWall) {
      const cfg = selectedWall.customConfig || {};
      const rawBlur = typeof cfg.blur === 'number' ? cfg.blur : 0;
      // 兼容历史 0~20px 老配置数据转为百分比
      const normalizedBlur = (rawBlur > 0 && rawBlur <= 20) ? Math.round((rawBlur / 20) * 100) : rawBlur;
      const newUni: UniversalWallpaperSettings = {
        scaling: cfg.scaling || 'cover',
        dimming: typeof cfg.dimming === 'number' ? cfg.dimming : 15,
        blur: normalizedBlur,
        volume: typeof cfg.volume === 'number' ? cfg.volume : 80,
        playbackRate: typeof cfg.playbackRate === 'number' ? cfg.playbackRate : 1.0,
        audioDucking: typeof cfg.audioDucking === 'boolean' ? cfg.audioDucking : true,
        interactive: typeof selectedWall.interactive === 'boolean' ? selectedWall.interactive : true,
        muted: typeof cfg.muted === 'boolean' ? cfg.muted : false
      };
      setUniversal(newUni);

      if (selectedWall.properties) {
        const initial: Record<string, any> = {};
        Object.entries(selectedWall.properties).forEach(([key, val]) => {
          if (typeof val === 'object' && val !== null && 'value' in val) {
            initial[key] = val.value;
          } else {
            initial[key] = val;
          }
        });
        setCustomProps(initial);
      } else {
        setCustomProps({});
      }

      syncAllPropsToPreview();
    }
  }, [selectedWall?.id]);

  const isCurrentSelectedRunning = selectedWall ? isWallpaperRunningOnDesktop(selectedWall) : false;
  const isSelectedVideo = selectedWall?.wallpaperType === 'video';
  const isSelectedImage = selectedWall?.wallpaperType === 'image';
  const hasCustomProperties = selectedWall?.properties && Object.keys(selectedWall.properties).length > 0;
  const hasAudioCapability = !isSelectedImage && (
    isSelectedVideo || 
    (selectedWall as any)?.declaredPermissions?.includes('audio') ||
    (selectedWall as any)?.permissions?.includes('audio') ||
    (selectedWall?.properties && ('audio' in selectedWall.properties || 'volume' in selectedWall.properties))
  );

  const handleUniversalChange = (key: keyof UniversalWallpaperSettings, val: any) => {
    setUniversal(prev => ({ ...prev, [key]: val }));
    onSendProperty(key, val, selectedWall?.id, selectedWall?.packagePath);
    postPropertyToPreview(key, val);
  };

  const handleCustomPropChange = (key: string, val: any) => {
    setCustomProps(prev => ({ ...prev, [key]: val }));
    onSendProperty(key, val, selectedWall?.id, selectedWall?.packagePath);
    postPropertyToPreview(key, val);
  };

  const handleResetDefaults = () => {
    const defaultUni: UniversalWallpaperSettings = {
      volume: 80,
      audioDucking: true,
      dimming: 15,
      blur: 0,
      scaling: 'cover',
      playbackRate: 1.0,
      interactive: true,
      muted: false
    };
    setUniversal(defaultUni);
    Object.entries(defaultUni).forEach(([k, v]) => {
      onSendProperty(k, v, selectedWall?.id, selectedWall?.packagePath);
      postPropertyToPreview(k, v);
    });

    if (selectedWall?.properties) {
      const initial: Record<string, any> = {};
      Object.entries(selectedWall.properties).forEach(([key, val]) => {
        const defVal = (typeof val === 'object' && val !== null && 'value' in val) ? val.value : val;
        initial[key] = defVal;
        onSendProperty(key, defVal, selectedWall?.id, selectedWall?.packagePath);
        postPropertyToPreview(key, defVal);
      });
      setCustomProps(initial);
    }
    onSendProperty('reset_defaults', true, selectedWall?.id, selectedWall?.packagePath);
    if (onApplyPreset) onApplyPreset();
  };

  const handleOpenEditModal = (wall: WallpaperInfo) => {
    if (!onEditCustomWallpaper) return;
    const isVideo = wall.wallpaperType === 'video';
    onEditCustomWallpaper({
      filePath: '',
      fileUrl: wall.mediaUrl || getWallpaperPreviewUrl(wall),
      name: wall.name,
      ext: isVideo ? 'MP4' : 'IMG',
      wallpaperType: isVideo ? 'video' : 'image',
      editWallpaperId: wall.id,
      packagePath: wall.packagePath,
      description: wall.description,
      customConfig: wall.customConfig || {
        scaling: universal.scaling,
        dimming: universal.dimming,
        blur: universal.blur,
        volume: universal.volume,
        playbackRate: universal.playbackRate,
        audioDucking: universal.audioDucking,
        muted: universal.muted
      }
    });
  };

  // Filter and Search
  const totalCount = wallpapers.length;
  const runningCount = wallpapers.filter(w => isWallpaperRunningOnDesktop(w)).length;
  const videoCount = wallpapers.filter(w => w.wallpaperType === 'video').length;
  const interactiveCount = wallpapers.filter(w => w.wallpaperType === 'html' || w.wallpaperType === 'web' || (!w.wallpaperType && !w.isCustom)).length;
  const customCount = wallpapers.filter(w => isCustomWallpaper(w)).length;

  const filteredWallpapers = wallpapers.filter(w => {
    if (searchQuery.trim()) {
      const q = searchQuery.toLowerCase();
      const match = (w.name || '').toLowerCase().includes(q) ||
                    (w.description || '').toLowerCase().includes(q) ||
                    (w.id || '').toLowerCase().includes(q);
      if (!match) return false;
    }
    if (filterMode === 'running') return isWallpaperRunningOnDesktop(w);
    if (filterMode === 'video') return w.wallpaperType === 'video';
    if (filterMode === 'interactive') return w.wallpaperType === 'html' || w.wallpaperType === 'web' || (!w.wallpaperType && !w.isCustom);
    if (filterMode === 'custom') return isCustomWallpaper(w);
    return true;
  });

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDraggingFile(true);
  };

  const handleDragLeave = () => {
    setIsDraggingFile(false);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDraggingFile(false);
    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0] as any;
      const targetPath = file.path || file.name;
      if (onImportMedia) {
        onImportMedia(targetPath);
      }
    }
  };

  const showDragActive = isDraggingFile || isExternalDragging;

  return (
    <div
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
      onDrop={handleDrop}
      className="relative space-y-4 animate-in fade-in duration-150"
    >
      {/* Full-Window Drag-Over Visual Overlay */}
      {showDragActive && (
        <div className="fixed inset-0 z-40 bg-indigo-950/35 backdrop-blur-xs flex items-center justify-center p-6 pointer-events-none animate-in fade-in duration-150">
          <div className="bg-white/95 border-2 border-dashed border-indigo-500 rounded-3xl p-8 max-w-md w-full text-center shadow-2xl flex flex-col items-center gap-3">
            <div className="w-14 h-14 rounded-2xl bg-indigo-600 text-white flex items-center justify-center shadow-lg shadow-indigo-600/25 animate-bounce">
              <Upload className="w-7 h-7" />
            </div>
            <h3 className="text-base font-bold text-slate-900">松开鼠标导入图片或视频壁纸</h3>
            <p className="text-xs text-slate-500 leading-relaxed">
              支持 MP4 / WebM / MOV 视频与 PNG / JPG / WebP / GIF 图片，松手后将打开实时预览与效果配置面板
            </p>
          </div>
        </div>
      )}

      {/* =========================================================================
          TOP STATUS BANNER: Clearly indicates which wallpaper is currently in use
         ========================================================================= */}
      <div
        className={`rounded-2xl px-4 py-3 border flex flex-wrap items-center justify-between gap-3 transition-all ${
          runningWall
            ? 'bg-emerald-50 dark:bg-emerald-950/60 border-emerald-200 dark:border-emerald-800/80 shadow-xs dark:shadow-md'
            : 'bg-white dark:bg-[#131927]/90 border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md'
        }`}
      >
        <div className="flex items-center gap-3 min-w-0">
          <div
            className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0 border ${
              runningWall
                ? 'bg-emerald-600 text-white border-emerald-500 shadow-xs'
                : 'bg-slate-100 dark:bg-slate-900 text-slate-500 dark:text-slate-400 border-slate-200 dark:border-slate-800'
            }`}
          >
            {runningWall ? <CheckCircle2 className="w-5 h-5" /> : <Sparkles className="w-4 h-4" />}
          </div>
          <div className="min-w-0">
            <div className="flex items-center gap-2">
              <span className="text-xs font-bold text-slate-900 dark:text-slate-100">
                {runningWall ? `当前桌面正在使用：${runningWall.name}` : '当前桌面未使用动态壁纸'}
              </span>
              {runningWall && (
                <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-bold bg-emerald-600 text-white shadow-xs">
                  <span className="w-1.5 h-1.5 rounded-full bg-white animate-pulse" />
                  运行中
                </span>
              )}
            </div>
            <p className="text-[11px] text-slate-500 dark:text-slate-400 truncate mt-0.5">
              {runningWall
                ? runningWall.description || '该动态壁纸正在桌面运行'
                : '正在显示 Windows 系统默认壁纸，点击下方壁纸卡片上的「设为壁纸」即可立即更换'}
            </p>
          </div>
        </div>

        {runningWall && (
          <div className="flex items-center gap-2 shrink-0">
            <button
              type="button"
              onClick={onStopWallpaper}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-rose-50 hover:bg-rose-100 dark:bg-rose-950/60 dark:hover:bg-rose-900/60 text-rose-700 dark:text-rose-300 border border-rose-200 dark:border-rose-800/60 transition-colors flex items-center gap-1.5 shadow-2xs cursor-pointer"
            >
              <Square className="w-3 h-3 fill-rose-600 dark:fill-rose-400 text-rose-600 dark:text-rose-400" />
              <span>关闭壁纸</span>
            </button>
          </div>
        )}
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-5">
        {/* =========================================================================
            LEFT 58% (7 Cols): Gallery
           ========================================================================= */}
        <div className="lg:col-span-7 flex flex-col gap-3.5">
          {/* Top Control Bar + Filter Pills */}
          <div className="bg-white dark:bg-[#131927]/90 rounded-2xl p-4 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md space-y-3">
            <div className="flex items-center justify-between gap-3">
              <div className="relative flex-1">
                <Search className="w-4 h-4 text-slate-400 dark:text-slate-500 absolute left-3 top-1/2 -translate-y-1/2" />
                <input
                  type="text"
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="搜索壁纸..."
                  className="w-full pl-9 pr-3 py-1.5 rounded-xl text-xs bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500 transition-all placeholder:text-slate-400 dark:placeholder:text-slate-500"
                />
              </div>

              <button
                type="button"
                onClick={() => {
                  if (onImportMedia) onImportMedia('New_Wallpaper');
                }}
                className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white transition-all shadow-xs flex items-center gap-1.5 shrink-0 active:scale-95 cursor-pointer"
              >
                <Plus className="w-3.5 h-3.5" />
                <span>导入壁纸</span>
              </button>
            </div>

            {/* Filter Pills */}
            <div className="flex items-center gap-1.5 pt-2 border-t border-slate-200/80 dark:border-slate-800/80 overflow-x-auto scrollbar-none">
              <button
                type="button"
                onClick={() => setFilterMode('all')}
                className={`px-3 py-1 rounded-lg text-xs font-medium transition-all shrink-0 cursor-pointer ${
                  filterMode === 'all'
                    ? 'bg-indigo-600 text-white font-semibold shadow-xs'
                    : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                全部 ({totalCount})
              </button>
              <button
                type="button"
                onClick={() => setFilterMode('running')}
                className={`px-3 py-1 rounded-lg text-xs font-medium transition-all shrink-0 cursor-pointer ${
                  filterMode === 'running'
                    ? 'bg-emerald-600 text-white font-semibold shadow-xs'
                    : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                运行中 ({runningCount})
              </button>
              <button
                type="button"
                onClick={() => setFilterMode('video')}
                className={`px-3 py-1 rounded-lg text-xs font-medium transition-all shrink-0 cursor-pointer ${
                  filterMode === 'video'
                    ? 'bg-indigo-600 text-white font-semibold shadow-xs'
                    : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                视频壁纸 ({videoCount})
              </button>
              <button
                type="button"
                onClick={() => setFilterMode('interactive')}
                className={`px-3 py-1 rounded-lg text-xs font-medium transition-all shrink-0 cursor-pointer ${
                  filterMode === 'interactive'
                    ? 'bg-indigo-600 text-white font-semibold shadow-xs'
                    : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                互动特效 ({interactiveCount})
              </button>
              <button
                type="button"
                onClick={() => setFilterMode('custom')}
                className={`px-3 py-1 rounded-lg text-xs font-medium transition-all shrink-0 cursor-pointer ${
                  filterMode === 'custom'
                    ? 'bg-amber-600 text-white font-semibold shadow-xs'
                    : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
                }`}
              >
                自定义 ({customCount})
              </button>
            </div>
          </div>

          {/* Interactive Drop Zone */}
          <div
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={handleDrop}
            onClick={() => {
              if (onImportMedia) onImportMedia('New_Wallpaper');
            }}
            className={`border border-dashed rounded-xl py-3 px-4 text-center transition-all duration-150 cursor-pointer ${
              showDragActive
                ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-950/40 scale-[1.005]'
                : 'border-slate-300 dark:border-slate-800 bg-white/80 dark:bg-[#131927]/60 hover:bg-indigo-50/50 dark:hover:bg-indigo-950/20 hover:border-indigo-400 dark:hover:border-indigo-500/50'
            }`}
          >
            <div className="flex items-center justify-center gap-2 text-slate-600 dark:text-slate-400 text-xs font-medium">
              <Upload className={`w-4 h-4 ${showDragActive ? 'text-indigo-600 dark:text-indigo-400 animate-bounce' : 'text-indigo-600 dark:text-indigo-400'}`} />
              <span>将视频 (MP4/WebM) 或图片 (PNG/JPG/WebP) 拖入窗口，或点击此处选择文件制作壁纸</span>
            </div>
          </div>

          {/* Gallery Cards Grid (Lightweight, No Background iframes) */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-3.5">
            {filteredWallpapers.map((wall) => {
              const isRunning = isWallpaperRunningOnDesktop(wall);
              const isSelected = selectedWall?.id === wall.id;
              const isCustom = isCustomWallpaper(wall);

              return (
                <div
                  key={wall.id}
                  onClick={() => setSelectedWallId(wall.id)}
                  className={`group relative rounded-2xl overflow-hidden transition-all duration-150 cursor-pointer flex flex-col justify-between ${
                    isRunning
                      ? 'border-2 border-emerald-500 bg-emerald-50/50 dark:bg-emerald-950/20 shadow-md ring-1 ring-emerald-500/20'
                      : isSelected
                      ? 'border-2 border-indigo-500 bg-white dark:bg-[#131927] shadow-md ring-1 ring-indigo-500/30'
                      : 'border border-slate-200/90 dark:border-slate-800/90 bg-white dark:bg-[#131927]/90 hover:border-slate-300 dark:hover:border-slate-700 shadow-xs hover:shadow-md'
                  }`}
                >
                  {/* 16:9 Static/Lightweight Thumbnail View */}
                  <div 
                    className="relative w-full aspect-video bg-black overflow-hidden select-none"
                    onMouseEnter={() => setHoveredWallId(wall.id)}
                    onMouseLeave={() => setHoveredWallId(null)}
                  >
                    <ThumbnailView wall={wall} isHovered={hoveredWallId === wall.id} />

                    {/* Prominent Top-Left Status Badge */}
                    <div className="absolute top-2.5 left-2.5 flex items-center gap-1.5">
                      {isRunning ? (
                        <span className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg bg-emerald-600 text-white text-[11px] font-bold shadow-md">
                          <span className="w-2 h-2 rounded-full bg-white animate-pulse" />
                          当前正在使用
                        </span>
                      ) : isSelected ? (
                        <span className="flex items-center gap-1 px-2 py-0.5 rounded-md bg-indigo-600/90 backdrop-blur-xs text-white text-[10px] font-semibold shadow-xs">
                          右侧预览中
                        </span>
                      ) : null}

                      {isCustom && (
                        <span className="flex items-center gap-1 px-2 py-0.5 rounded-md bg-slate-900/80 backdrop-blur-xs text-white text-[10px] font-medium border border-white/10">
                          {wall.wallpaperType === 'video' ? (
                            <Film className="w-2.5 h-2.5 text-indigo-300" />
                          ) : (
                            <ImageIcon className="w-2.5 h-2.5 text-emerald-300" />
                          )}
                          自定义
                        </span>
                      )}

                      {wall.wallpaperType === 'video' && wall.isArchived === false && (
                        <span className="flex items-center gap-1 px-1.5 py-0.5 rounded-md bg-amber-500/90 backdrop-blur-xs text-white text-[10px] font-medium shadow-xs">
                          就地引用
                        </span>
                      )}

                      {wall.isMissing && (
                        <span className="flex items-center gap-1 px-1.5 py-0.5 rounded-md bg-rose-600/95 backdrop-blur-xs text-white text-[10px] font-bold shadow-xs">
                          源文件丢失
                        </span>
                      )}
                    </div>

                    {/* Top-Right Quick Management Actions (Edit / Folder / Delete) */}
                    <div className="absolute top-2 right-2 flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity duration-150">
                      {isCustom && onEditCustomWallpaper && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            handleOpenEditModal(wall);
                          }}
                          className="p-1.5 rounded-lg bg-slate-900/80 hover:bg-indigo-600 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer border border-white/10"
                          title="编辑壁纸效果与参数"
                        >
                          <Settings2 className="w-3.5 h-3.5" />
                        </button>
                      )}

                      {wall.packagePath && onOpenFolder && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            onOpenFolder(wall.id, wall.packagePath);
                          }}
                          className="p-1.5 rounded-lg bg-slate-900/80 hover:bg-slate-800 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer border border-white/10"
                          title="打开所在文件夹"
                        >
                          <FolderOpen className="w-3.5 h-3.5" />
                        </button>
                      )}

                      {isCustom && onDeleteWallpaper && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            setWallpaperToDelete(wall);
                          }}
                          className="p-1.5 rounded-lg bg-slate-900/80 hover:bg-rose-600 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer border border-white/10"
                          title="删除此壁纸"
                        >
                          <Trash2 className="w-3.5 h-3.5" />
                        </button>
                      )}
                    </div>
                  </div>

                  {/* Card Footer with Direct Action Button */}
                  <div className="p-3 flex items-center justify-between gap-2 bg-white dark:bg-[#131927]">
                    <div className="min-w-0">
                      <div className="flex items-center gap-1.5">
                        <h4 className="text-xs font-bold text-slate-900 dark:text-slate-100 truncate">{wall.name}</h4>
                      </div>
                      <p className="text-[11px] text-slate-500 dark:text-slate-400 line-clamp-1 mt-0.5">
                        {wall.description || (isCustom ? '用户导入壁纸' : '官方动态壁纸')}
                      </p>
                    </div>

                    <div className="flex items-center gap-1.5 shrink-0">
                      {isRunning ? (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            onStopWallpaper();
                          }}
                          className="px-2.5 py-1.5 rounded-lg text-[11px] font-bold bg-emerald-50 dark:bg-emerald-950/80 text-emerald-700 dark:text-emerald-300 hover:bg-rose-50 dark:hover:bg-rose-950/60 hover:text-rose-700 dark:hover:text-rose-300 border border-emerald-200 dark:border-emerald-800/80 hover:border-rose-200 dark:hover:border-rose-800/60 transition-colors shrink-0 flex items-center gap-1 cursor-pointer"
                          title="点击关闭动态壁纸"
                        >
                          <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 dark:text-emerald-400" />
                          <span>使用中</span>
                        </button>
                      ) : (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            setSelectedWallId(wall.id);
                            if (wall.isMissing) {
                              onRelocateWallpaper?.(wall.id);
                            } else {
                              onApplyWallpaper(wall.id, wall.packagePath);
                            }
                          }}
                          className={`px-3 py-1.5 rounded-lg text-[11px] font-semibold transition-all shrink-0 flex items-center gap-1 shadow-2xs active:scale-95 cursor-pointer ${
                            wall.isMissing
                              ? 'bg-amber-500 hover:bg-amber-600 text-white'
                              : 'bg-indigo-600 hover:bg-indigo-700 text-white'
                          }`}
                          title={wall.isMissing ? '源文件已丢失，点击重新选择文件' : '设为壁纸'}
                        >
                          {wall.isMissing ? (
                            <>
                              <FolderOpen className="w-3 h-3" />
                              <span>重新定位</span>
                            </>
                          ) : (
                            <>
                              <Play className="w-3 h-3 fill-white" />
                              <span>设为壁纸</span>
                            </>
                          )}
                        </button>
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>

          {filteredWallpapers.length === 0 && (
            <div className="py-12 text-center bg-white dark:bg-[#131927]/90 rounded-2xl border border-slate-200/90 dark:border-slate-800/90 shadow-xs">
              <ImageOff className="w-8 h-8 text-slate-400 dark:text-slate-500 mx-auto mb-2" />
              <p className="text-xs text-slate-600 dark:text-slate-400">未找到符合条件的壁纸</p>
            </div>
          )}
        </div>

        {/* =========================================================================
            RIGHT 42% (5 Cols): Real-time Preview + Pure Parameters
           ========================================================================= */}
        <div className="lg:col-span-5 flex flex-col gap-3.5">
          {selectedWall ? (
            <>
              {/* 16:9 Canvas Viewport */}
              <div className="bg-white dark:bg-[#131927]/90 rounded-2xl p-3.5 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md flex flex-col gap-2.5">
                <div className="flex items-center justify-between gap-2">
                  <div className="flex items-center gap-2 min-w-0">
                    <Eye className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400 shrink-0" />
                    <span className="text-xs font-bold text-slate-900 dark:text-slate-100 truncate">{selectedWall.name}</span>
                  </div>
                  <div className="flex items-center gap-1.5 shrink-0">
                    {/* Desktop Icon Mock Overlay Toggle */}
                    <button
                      type="button"
                      onClick={() => setShowDesktopIcons(!showDesktopIcons)}
                      className={`px-2 py-0.5 rounded-md text-[10px] font-semibold border flex items-center gap-1 transition-colors cursor-pointer ${
                        showDesktopIcons
                          ? 'bg-indigo-600 text-white border-indigo-600 shadow-xs'
                          : 'bg-slate-100 dark:bg-slate-900 text-slate-700 dark:text-slate-300 border-slate-200 dark:border-slate-800 hover:bg-slate-200/70 dark:hover:bg-slate-800'
                      }`}
                      title="显示/隐藏模拟桌面图标，测试图标对比度与易读性"
                    >
                      <Monitor className="w-3 h-3" />
                      <span>桌面图标透视</span>
                    </button>

                    {isCustomWallpaper(selectedWall) && onEditCustomWallpaper && (
                      <button
                        type="button"
                        onClick={() => handleOpenEditModal(selectedWall)}
                        className="px-2 py-0.5 rounded-md text-[10px] font-semibold bg-indigo-50 dark:bg-indigo-950/60 hover:bg-indigo-100 dark:hover:bg-indigo-900/60 text-indigo-700 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-800/60 flex items-center gap-1 transition-colors cursor-pointer"
                      >
                        <Settings2 className="w-3 h-3" />
                        <span>配置</span>
                      </button>
                    )}
                    {isCurrentSelectedRunning ? (
                      <span className="px-2 py-0.5 rounded-full text-[10px] font-bold bg-emerald-50 dark:bg-emerald-950/80 text-emerald-700 dark:text-emerald-300 border border-emerald-200 dark:border-emerald-800/80 shrink-0">
                        ● 桌面使用中
                      </span>
                    ) : (
                      <span className="px-2 py-0.5 rounded-full text-[10px] font-medium bg-slate-100 dark:bg-slate-800 text-slate-600 dark:text-slate-400 border border-slate-200 dark:border-slate-700/50 shrink-0">
                        预览模式
                      </span>
                    )}
                  </div>
                </div>

                {/* Real 16:9 Canvas */}
                <div className="relative w-full aspect-video rounded-xl overflow-hidden bg-black border border-slate-300/80 dark:border-slate-900/60 select-none shadow-inner">
                  {selectedWall.wallpaperType === 'image' ? (
                    /* Static Image: Direct <img> render with instant CSS filters */
                    <div className="relative w-full h-full overflow-hidden bg-black">
                      <img
                        key={selectedWall.id}
                        src={
                          selectedWall.previewUrl ||
                          (selectedWall.sourcePath
                            ? `${selectedWall.sourcePath.replace(/\\/g, '/').startsWith('/') ? 'file://' : 'file:///'}${selectedWall.sourcePath.replace(/\\/g, '/')}`
                            : '') ||
                          selectedWall.mediaUrl ||
                          ''
                        }
                        alt={selectedWall.name}
                        className="w-full h-full pointer-events-none select-none transition-all duration-150"
                        style={{
                          objectFit: universal.scaling === 'contain' ? 'contain' : universal.scaling === 'fill' ? 'fill' : 'cover',
                          filter: blurPercentToPx(universal.blur) > 0 ? `blur(${blurPercentToPx(universal.blur).toFixed(1)}px)` : 'none',
                          transform: blurPercentToPx(universal.blur) > 0 ? 'scale(1.04)' : 'scale(1)'
                        }}
                      />
                    </div>
                  ) : selectedWall.wallpaperType === 'video' ? (
                    /* Video Wallpaper: Direct Native Media Foundation Streaming Player or Poster */
                    <div className="relative w-full h-full overflow-hidden bg-black">
                      <VideoLiveCanvas
                        videoPath={selectedWall.mediaUrl || selectedWall.sourcePath || selectedWall.packagePath || ''}
                        posterUrl={selectedWall.previewUrl}
                        isActive={true}
                        fps={30}
                        objectFit={universal.scaling === 'contain' ? 'contain' : universal.scaling === 'fill' ? 'fill' : 'cover'}
                        className="w-full h-full"
                        style={{
                          filter: blurPercentToPx(universal.blur) > 0 ? `blur(${blurPercentToPx(universal.blur).toFixed(1)}px)` : 'none',
                          transform: blurPercentToPx(universal.blur) > 0 ? 'scale(1.04)' : 'scale(1)'
                        }}
                      />
                    </div>
                  ) : (
                    /* HTML / WebGL / Image Canvas: High-Fidelity Virtual Desktop Viewport Engine */
                    <VirtualWallpaperViewport
                      wall={selectedWall}
                      src={getWallpaperPreviewUrl(selectedWall)}
                      onLoad={syncAllPropsToPreview}
                      iframeRef={previewIframeRef}
                      filterStyle={{
                        filter: blurPercentToPx(universal.blur) > 0 ? `blur(${blurPercentToPx(universal.blur).toFixed(1)}px)` : 'none',
                      }}
                    />
                  )}

                  {/* Missing File Clean Indicator Overlay */}
                  {selectedWall.isMissing && (
                    <div className="absolute inset-0 bg-slate-950/75 backdrop-blur-xs flex flex-col items-center justify-center p-4 text-center z-10 select-none">
                      <div className="w-9 h-9 rounded-xl bg-amber-500/20 text-amber-400 border border-amber-500/30 flex items-center justify-center mb-2 shadow-xs">
                        <FolderOpen className="w-4 h-4" />
                      </div>
                      <p className="text-xs font-semibold text-white mb-0.5">源文件已丢失或移动</p>
                      <p className="text-[11px] text-slate-300 font-mono break-all max-w-xs mb-3">
                        {selectedWall.sourcePath || selectedWall.packagePath}
                      </p>
                      {onRelocateWallpaper && (
                        <button
                          type="button"
                          onClick={() => onRelocateWallpaper(selectedWall.id)}
                          className="px-3 py-1.5 rounded-lg bg-amber-500 hover:bg-amber-600 text-white font-medium text-xs shadow-xs transition-colors flex items-center gap-1.5 cursor-pointer active:scale-95"
                        >
                          <FolderOpen className="w-3.5 h-3.5" />
                          <span>重新定位文件</span>
                        </button>
                      )}
                    </div>
                  )}

                  {/* Dimming overlay if configured */}
                  {universal.dimming > 0 && (
                    <div
                      className="absolute inset-0 bg-black pointer-events-none transition-opacity duration-150"
                      style={{ opacity: universal.dimming / 100 }}
                    />
                  )}

                  {/* Simulated Desktop Icons Overlay for Contrast / Readability Preview */}
                  {showDesktopIcons && (
                    <div className="absolute top-3 left-3 flex flex-col gap-2.5 pointer-events-none opacity-90 animate-in fade-in duration-100">
                      <div className="flex flex-col items-center gap-1 w-12">
                        <div className="w-7 h-7 rounded-lg bg-white/20 backdrop-blur-xs border border-white/30 shadow-xs flex items-center justify-center text-[10px] text-white">
                          💻
                        </div>
                        <span className="text-[9px] text-white font-medium drop-shadow-xs truncate w-full text-center">
                          此电脑
                        </span>
                      </div>
                      <div className="flex flex-col items-center gap-1 w-12">
                        <div className="w-7 h-7 rounded-lg bg-indigo-500/30 backdrop-blur-xs border border-white/30 shadow-xs flex items-center justify-center text-[10px] text-white">
                          🗑️
                        </div>
                        <span className="text-[9px] text-white font-medium drop-shadow-xs truncate w-full text-center">
                          回收站
                        </span>
                      </div>
                    </div>
                  )}
                </div>
              </div>

              {/* In-place reference badge & Archive action */}
              {selectedWall.wallpaperType === 'video' && !selectedWall.isArchived && !selectedWall.isMissing && onArchiveWallpaper && (
                <div className="bg-amber-50 dark:bg-amber-950/40 border border-amber-200 dark:border-amber-800/60 rounded-xl p-3 flex items-center justify-between gap-3 text-xs shadow-2xs">
                  <div className="flex items-center gap-2 min-w-0">
                    <span className="w-2 h-2 rounded-full bg-amber-500 shrink-0" />
                    <div className="min-w-0">
                      <p className="font-semibold text-amber-900 dark:text-amber-200 truncate">当前视频为本地就地引用（零拷贝存储）</p>
                      <p className="text-[11px] text-amber-700/80 dark:text-amber-400/80 truncate mt-0.5">
                        {selectedWall.fileSizeBytes
                          ? `文件大小 ${(selectedWall.fileSizeBytes / (1024 * 1024)).toFixed(1)} MB，原文件重命名或移动可能导致壁纸失效`
                          : '原文件重命名或移动可能导致壁纸失效'}
                      </p>
                    </div>
                  </div>
                  <button
                    type="button"
                    onClick={() => onArchiveWallpaper(selectedWall.id)}
                    className="shrink-0 px-3 py-1.5 rounded-lg bg-amber-500 hover:bg-amber-600 text-white font-medium text-[11px] shadow-xs transition-colors cursor-pointer"
                  >
                    归档到壁纸库
                  </button>
                </div>
              )}

              {/* Customizer Card */}
              <div className="bg-white dark:bg-[#131927]/90 rounded-2xl p-4 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md space-y-4">
                {/* Dynamic Author Properties */}
                {hasCustomProperties && (
                  <div>
                    <div className="flex items-center gap-1.5 mb-2.5 pb-2 border-b border-slate-200/80 dark:border-slate-800/80">
                      <Sliders className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                      <span className="text-xs font-bold text-slate-900 dark:text-slate-100">壁纸个性设置</span>
                    </div>

                    <div className="space-y-3">
                      {Object.entries(selectedWall.properties!).map(([key, schema]: [string, any]) => {
                        const type = schema.type || 'slider';
                        const label = schema.text || key;
                        const val = customProps[key] !== undefined ? customProps[key] : schema.value;

                        return (
                          <div key={key} className="space-y-1">
                            <div className="flex items-center justify-between text-xs">
                              <span className="text-slate-700 dark:text-slate-300 font-medium">{label}</span>
                              <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{String(val)}</span>
                            </div>

                            {type === 'slider' && (
                              <input
                                type="range"
                                min={schema.min ?? 0}
                                max={schema.max ?? 100}
                                step={schema.step ?? 1}
                                value={Number(val) || 0}
                                onChange={(e) => handleCustomPropChange(key, parseFloat(e.target.value))}
                                className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                              />
                            )}

                            {type === 'color' && (
                              <div className="flex items-center gap-2">
                                <input
                                  type="color"
                                  value={String(val || '#6366f1')}
                                  onChange={(e) => handleCustomPropChange(key, e.target.value)}
                                  className="w-7 h-7 rounded border border-slate-300 dark:border-slate-700 bg-white dark:bg-slate-900 cursor-pointer shadow-2xs"
                                />
                                <span className="text-xs font-mono text-slate-500 dark:text-slate-400">{String(val || '#6366f1')}</span>
                              </div>
                            )}

                            {type === 'boolean' && (
                              <button
                                type="button"
                                onClick={() => handleCustomPropChange(key, !val)}
                                className={`relative inline-flex h-4.5 w-8 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ${
                                  val ? 'bg-indigo-600' : 'bg-slate-300 dark:bg-slate-700'
                                }`}
                              >
                                <span className={`inline-block h-3.5 w-3.5 transform rounded-full bg-white shadow-xs transition duration-200 ${
                                  val ? 'translate-x-3.5' : 'translate-x-0'
                                }`} />
                              </button>
                            )}
                          </div>
                        );
                      })}
                    </div>
                  </div>
                )}

                {/* Display & Playback Controls */}
                {!hasCustomProperties ? (
                  /* 场景 1: 普通视频与普通壁纸 (无开发者专有属性) -> 直接完全展开呈现，绝不折叠隐藏！ */
                  <div className="space-y-3.5">
                    <div className="flex items-center gap-1.5 pb-2 border-b border-slate-200/80 dark:border-slate-800/80">
                      <span className="text-xs font-bold text-slate-900 dark:text-slate-100">播放与视觉效果</span>
                    </div>

                    {/* Scaling Mode (画面填充比例) */}
                    {(isSelectedImage || isSelectedVideo) && (
                      <div className="space-y-1.5">
                        <div className="flex items-center justify-between text-xs">
                          <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                            <Maximize2 className="w-3.5 h-3.5 text-slate-400" />
                            画面填充比例
                          </span>
                        </div>
                        <div className="grid grid-cols-3 gap-1.5">
                          {([
                            { key: 'cover', label: '铺满裁切' },
                            { key: 'contain', label: '完整适应' },
                            { key: 'fill', label: '拉伸填满' }
                          ] as const).map((item) => (
                            <button
                              key={item.key}
                              type="button"
                              onClick={() => handleUniversalChange('scaling', item.key)}
                              className={`py-1 px-2 rounded-lg text-[11px] font-semibold border transition-all cursor-pointer ${
                                universal.scaling === item.key
                                  ? 'bg-indigo-600 text-white border-indigo-600 shadow-xs'
                                  : 'bg-white dark:bg-slate-900 text-slate-700 dark:text-slate-400 border-slate-200 dark:border-slate-800 hover:bg-slate-100 dark:hover:bg-slate-800'
                              }`}
                            >
                              {item.label}
                            </button>
                          ))}
                        </div>
                      </div>
                    )}

                    {/* Desktop Readability Dimming (壁纸压暗) */}
                    <div className="space-y-1">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                          <Moon className="w-3.5 h-3.5 text-slate-400" />
                          壁纸压暗程度
                        </span>
                        <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{universal.dimming}%</span>
                      </div>
                      <input
                        type="range"
                        min={0}
                        max={80}
                        value={universal.dimming}
                        onChange={(e) => handleUniversalChange('dimming', parseInt(e.target.value, 10))}
                        className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                      />
                    </div>

                    {/* Background Blur (背景模糊度 0% ~ 100%) */}
                    <div className="space-y-1">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                          <Sparkles className="w-3.5 h-3.5 text-slate-400" />
                          背景模糊度
                        </span>
                        <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{universal.blur}%</span>
                      </div>
                      <input
                        type="range"
                        min={0}
                        max={100}
                        step={1}
                        value={universal.blur}
                        onChange={(e) => handleUniversalChange('blur', parseInt(e.target.value, 10))}
                        className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                      />
                    </div>

                    {/* Volume (仅非纯静态图片) */}
                    {!isSelectedImage && (
                      <div className="space-y-1">
                        <div className="flex items-center justify-between text-xs">
                          <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                            {universal.muted || universal.volume === 0 ? (
                              <VolumeX className="w-3.5 h-3.5 text-rose-500 dark:text-rose-400" />
                            ) : (
                              <Volume2 className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                            )}
                            音量
                          </span>
                          <div className="flex items-center gap-2">
                            <button
                              type="button"
                              onClick={() => handleUniversalChange('muted', !universal.muted)}
                              className={`px-1.5 py-0.5 rounded text-[10px] font-bold cursor-pointer transition-colors ${
                                universal.muted
                                  ? 'bg-rose-50 dark:bg-rose-950/80 text-rose-700 dark:text-rose-300 border border-rose-200 dark:border-rose-800/60'
                                  : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800'
                              }`}
                            >
                              {universal.muted ? '已静音' : '静音'}
                            </button>
                            <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">
                              {universal.muted ? '0%' : `${universal.volume}%`}
                            </span>
                          </div>
                        </div>
                        <input
                          type="range"
                          min={0}
                          max={100}
                          disabled={universal.muted}
                          value={universal.muted ? 0 : universal.volume}
                          onChange={(e) => {
                            if (universal.muted) handleUniversalChange('muted', false);
                            handleUniversalChange('volume', parseInt(e.target.value, 10));
                          }}
                          className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500 disabled:opacity-40"
                        />
                      </div>
                    )}

                    {/* Playback Rate (仅视频) */}
                    {isSelectedVideo && (
                      <div className="space-y-1.5">
                        <div className="flex items-center justify-between text-xs">
                          <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                            <Gauge className="w-3.5 h-3.5 text-slate-400" />
                            视频播放倍速
                          </span>
                          <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{universal.playbackRate}x</span>
                        </div>
                        <div className="grid grid-cols-6 gap-1">
                          {[0.5, 0.75, 1.0, 1.25, 1.5, 2.0].map((rate) => (
                            <button
                              key={rate}
                              type="button"
                              onClick={() => handleUniversalChange('playbackRate', rate)}
                              className={`py-1 rounded-md text-[10px] font-bold border transition-all cursor-pointer ${
                                universal.playbackRate === rate
                                  ? 'bg-indigo-600 text-white border-indigo-600 shadow-xs'
                                  : 'bg-white dark:bg-slate-900 text-slate-700 dark:text-slate-400 border-slate-200 dark:border-slate-800 hover:bg-slate-100 dark:hover:bg-slate-800'
                              }`}
                            >
                              {rate}x
                            </button>
                          ))}
                        </div>
                      </div>
                    )}

                    {/* Audio Ducking (仅非纯静态图片) */}
                    {!isSelectedImage && (
                      <label className="flex items-center justify-between p-2 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200 dark:border-slate-800/80 cursor-pointer">
                        <span className="text-xs font-medium text-slate-700 dark:text-slate-300">其他软件发声时自动静音</span>
                        <input
                          type="checkbox"
                          checked={universal.audioDucking}
                          onChange={(e) => handleUniversalChange('audioDucking', e.target.checked)}
                          className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4 bg-white dark:bg-slate-800 border-slate-300 dark:border-slate-700"
                        />
                      </label>
                    )}

                    {/* Interactive Toggle */}
                    <label className="flex items-center justify-between p-2 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200 dark:border-slate-800/80 cursor-pointer">
                      <span className="text-xs font-medium text-slate-700 dark:text-slate-300">支持鼠标互动效果</span>
                      <input
                        type="checkbox"
                        checked={universal.interactive}
                        onChange={(e) => handleUniversalChange('interactive', e.target.checked)}
                        className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4 bg-white dark:bg-slate-800 border-slate-300 dark:border-slate-700"
                      />
                    </label>
                  </div>
                ) : (
                  /* 场景 2: 具有开发者自定义属性的壁纸 -> 上方已展示专有控制，下方宿主外挂支持折叠收起 */
                  <div className="rounded-xl border border-slate-200/80 dark:border-slate-800/80 bg-slate-50/50 dark:bg-slate-900/40 overflow-hidden transition-all">
                    <button
                      type="button"
                      onClick={() => setIsHostControlsOpen(!isHostControlsOpen)}
                      className="w-full flex items-center justify-between p-3 cursor-pointer hover:bg-slate-100/60 dark:hover:bg-slate-800/60 transition-colors text-left select-none"
                    >
                      <div className="flex items-center gap-2">
                        <Sliders className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                        <span className="text-xs font-bold text-slate-900 dark:text-slate-100">
                          宿主通用视觉与外挂
                        </span>
                        <span className="text-[10px] font-medium text-slate-500 dark:text-slate-400 bg-slate-200/80 dark:bg-slate-800 px-1.5 py-0.5 rounded">
                          宿主外挂
                        </span>
                      </div>
                      <div className="flex items-center gap-1 text-slate-400 dark:text-slate-500 text-[11px] font-medium">
                        <span>{isHostControlsOpen ? '收起' : '展开'}</span>
                        {isHostControlsOpen ? (
                          <ChevronDown className="w-3.5 h-3.5" />
                        ) : (
                          <ChevronRight className="w-3.5 h-3.5" />
                        )}
                      </div>
                    </button>

                    {isHostControlsOpen && (
                      <div className="p-3 pt-1 space-y-3.5 border-t border-slate-200/60 dark:border-slate-800/60 animate-in fade-in duration-150">
                        {/* Desktop Readability Dimming */}
                        <div className="space-y-1">
                          <div className="flex items-center justify-between text-xs">
                            <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                              <Moon className="w-3.5 h-3.5 text-slate-400" />
                              壁纸压暗程度
                            </span>
                            <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{universal.dimming}%</span>
                          </div>
                          <input
                            type="range"
                            min={0}
                            max={80}
                            value={universal.dimming}
                            onChange={(e) => handleUniversalChange('dimming', parseInt(e.target.value, 10))}
                            className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                          />
                        </div>

                        {/* Background Blur (Percentage 0% ~ 100%) */}
                        <div className="space-y-1">
                          <div className="flex items-center justify-between text-xs">
                            <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                              <Sparkles className="w-3.5 h-3.5 text-slate-400" />
                              背景模糊度
                            </span>
                            <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{universal.blur}%</span>
                          </div>
                          <input
                            type="range"
                            min={0}
                            max={100}
                            step={1}
                            value={universal.blur}
                            onChange={(e) => handleUniversalChange('blur', parseInt(e.target.value, 10))}
                            className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                          />
                        </div>

                        {/* Volume (Only if wallpaper carries audio) */}
                        {hasAudioCapability && (
                          <div className="space-y-1">
                            <div className="flex items-center justify-between text-xs">
                              <span className="text-slate-700 dark:text-slate-300 font-medium flex items-center gap-1.5">
                                {universal.muted || universal.volume === 0 ? (
                                  <VolumeX className="w-3.5 h-3.5 text-rose-500 dark:text-rose-400" />
                                ) : (
                                  <Volume2 className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                                )}
                                音量
                              </span>
                              <div className="flex items-center gap-2">
                                <button
                                  type="button"
                                  onClick={() => handleUniversalChange('muted', !universal.muted)}
                                  className={`px-1.5 py-0.5 rounded text-[10px] font-bold cursor-pointer transition-colors ${
                                    universal.muted
                                      ? 'bg-rose-50 dark:bg-rose-950/80 text-rose-700 dark:text-rose-300 border border-rose-200 dark:border-rose-800/60'
                                      : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800'
                                  }`}
                                >
                                  {universal.muted ? '已静音' : '静音'}
                                </button>
                                <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">
                                  {universal.muted ? '0%' : `${universal.volume}%`}
                                </span>
                              </div>
                            </div>
                            <input
                              type="range"
                              min={0}
                              max={100}
                              disabled={universal.muted}
                              value={universal.muted ? 0 : universal.volume}
                              onChange={(e) => {
                                if (universal.muted) handleUniversalChange('muted', false);
                                handleUniversalChange('volume', parseInt(e.target.value, 10));
                              }}
                              className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500 disabled:opacity-40"
                            />
                          </div>
                        )}

                        {/* Audio Ducking (Only for audio-carrying wallpapers) */}
                        {hasAudioCapability && (
                          <label className="flex items-center justify-between p-2 rounded-xl bg-white/70 dark:bg-slate-900/60 border border-slate-200 dark:border-slate-800/80 cursor-pointer">
                            <span className="text-xs font-medium text-slate-700 dark:text-slate-300">其他软件发声时自动静音</span>
                            <input
                              type="checkbox"
                              checked={universal.audioDucking}
                              onChange={(e) => handleUniversalChange('audioDucking', e.target.checked)}
                              className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4 bg-white dark:bg-slate-800 border-slate-300 dark:border-slate-700"
                            />
                          </label>
                        )}

                        {/* Interactive Toggle */}
                        <label className="flex items-center justify-between p-2 rounded-xl bg-white/70 dark:bg-slate-900/60 border border-slate-200 dark:border-slate-800/80 cursor-pointer">
                          <span className="text-xs font-medium text-slate-700 dark:text-slate-300">支持鼠标互动效果</span>
                          <input
                            type="checkbox"
                            checked={universal.interactive}
                            onChange={(e) => handleUniversalChange('interactive', e.target.checked)}
                            className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4 bg-white dark:bg-slate-800 border-slate-300 dark:border-slate-700"
                          />
                        </label>
                      </div>
                    )}
                  </div>
                )}

                {/* Action Buttons */}
                <div className="pt-2 border-t border-slate-200/80 dark:border-slate-800/80 flex items-center gap-2">
                  {isCurrentSelectedRunning ? (
                    <button
                      type="button"
                      onClick={onStopWallpaper}
                      className="flex-1 py-2.5 px-4 rounded-xl text-xs font-bold bg-rose-50 hover:bg-rose-100 dark:bg-rose-950/60 dark:hover:bg-rose-900/60 text-rose-700 dark:text-rose-300 border border-rose-200 dark:border-rose-800/60 transition-colors flex items-center justify-center gap-2 shadow-2xs cursor-pointer"
                    >
                      <Square className="w-3.5 h-3.5 text-rose-600 dark:text-rose-400 fill-rose-600 dark:fill-rose-400" />
                      <span>关闭桌面壁纸</span>
                    </button>
                  ) : selectedWall.isMissing ? (
                    <button
                      type="button"
                      onClick={() => onRelocateWallpaper?.(selectedWall.id)}
                      className="flex-1 py-2.5 px-4 rounded-xl text-xs font-bold bg-amber-500 hover:bg-amber-600 text-white transition-all shadow-md shadow-amber-500/20 active:scale-[0.98] flex items-center justify-center gap-2 cursor-pointer"
                      title="点击重新选择源文件路径"
                    >
                      <FolderOpen className="w-3.5 h-3.5" />
                      <span>重新定位源文件</span>
                    </button>
                  ) : (
                    <button
                      type="button"
                      onClick={() => onApplyWallpaper(selectedWall.id, selectedWall.packagePath)}
                      className="flex-1 py-2.5 px-4 rounded-xl text-xs font-bold bg-indigo-600 hover:bg-indigo-500 text-white transition-all shadow-md shadow-indigo-600/30 active:scale-[0.98] flex items-center justify-center gap-2 cursor-pointer"
                    >
                      <Play className="w-3.5 h-3.5 fill-white" />
                      <span>设为当前桌面壁纸</span>
                    </button>
                  )}

                  {isCustomWallpaper(selectedWall) && onDeleteWallpaper && (
                    <button
                      type="button"
                      onClick={() => setWallpaperToDelete(selectedWall)}
                      className="p-2.5 rounded-xl text-xs font-medium text-rose-600 dark:text-rose-400 hover:bg-rose-50 dark:hover:bg-rose-950/50 transition-colors cursor-pointer border border-rose-200 dark:border-rose-900/30"
                      title="删除此自定义壁纸"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  )}

                  <button
                    type="button"
                    onClick={handleResetDefaults}
                    className="p-2.5 rounded-xl text-xs font-medium text-slate-500 dark:text-slate-400 hover:text-slate-800 dark:hover:text-slate-100 bg-white dark:bg-transparent hover:bg-slate-100 dark:hover:bg-slate-800 transition-colors cursor-pointer border border-slate-200 dark:border-slate-800 shadow-2xs"
                    title="恢复默认参数"
                  >
                    <RefreshCw className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>
            </>
          ) : (
            <div className="py-24 text-center bg-white dark:bg-[#131927]/90 rounded-2xl border border-slate-200/90 dark:border-slate-800/90 shadow-xs">
              <ImageOff className="w-8 h-8 text-slate-400 dark:text-slate-500 mx-auto mb-2" />
              <p className="text-xs text-slate-600 dark:text-slate-400">请选择左侧壁纸</p>
            </div>
          )}
        </div>
      </div>

      {/* Wallpaper Deletion Secondary Confirmation Modal */}
      <WallpaperDeleteModal
        wallpaper={wallpaperToDelete}
        isActive={wallpaperToDelete ? (wallpaperToDelete.id === activeWallpaperId || wallpaperToDelete.packagePath === activeWallpaperId) : false}
        onClose={() => setWallpaperToDelete(null)}
        onConfirm={(id, pkgPath, name) => {
          if (onDeleteWallpaper) {
            onDeleteWallpaper(id, pkgPath, name);
          }
        }}
      />
    </div>
  );
};
