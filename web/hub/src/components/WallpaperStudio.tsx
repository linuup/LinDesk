import React, { useState, useEffect, useRef } from 'react';
import { 
  Play, Square, Sliders, ImageOff, Search, Plus, 
  Upload, Volume2, Moon, RefreshCw, Eye, CheckCircle2, Sparkles,
  Trash2, FolderOpen, Settings2, Maximize2, Gauge, Film, Image as ImageIcon
} from 'lucide-react';
import { WallpaperInfo, UniversalWallpaperSettings, StagedWallpaperMedia } from '../types';

interface WallpaperStudioProps {
  wallpapers: WallpaperInfo[];
  activeWallpaperId: string;
  isExternalDragging?: boolean;
  onApplyWallpaper: (id: string, packagePath?: string) => void;
  onStopWallpaper: () => void;
  onSendProperty: (property: string, value: any, widgetId?: string, packagePath?: string) => void;
  onApplyPreset?: () => void;
  onImportMedia?: (name: string) => void;
  onEditCustomWallpaper?: (media: StagedWallpaperMedia) => void;
  onDeleteWallpaper?: (id: string, packagePath: string, name: string) => void;
  onOpenFolder?: (id: string, packagePath: string) => void;
}

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

export const WallpaperStudio: React.FC<WallpaperStudioProps> = ({
  wallpapers,
  activeWallpaperId,
  isExternalDragging = false,
  onApplyWallpaper,
  onStopWallpaper,
  onSendProperty,
  onApplyPreset,
  onImportMedia,
  onEditCustomWallpaper,
  onDeleteWallpaper,
  onOpenFolder
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
    if (activeWallpaperId) return activeWallpaperId;
    return wallpapers.length > 0 ? wallpapers[0].id : '';
  });

  const [searchQuery, setSearchQuery] = useState('');
  const [isDraggingFile, setIsDraggingFile] = useState(false);

  const [universal, setUniversal] = useState<UniversalWallpaperSettings>({
    volume: 80,
    audioDucking: true,
    dimming: 15,
    blur: 0,
    scaling: 'cover',
    playbackRate: 1.0,
    interactive: true
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
      // Ignore cross-origin or unloaded iframe errors
    }
  };

  const syncAllPropsToPreview = () => {
    Object.entries(universal).forEach(([k, v]) => postPropertyToPreview(k, v));
    Object.entries(customProps).forEach(([k, v]) => postPropertyToPreview(k, v));
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
      setUniversal({
        scaling: cfg.scaling || 'cover',
        dimming: typeof cfg.dimming === 'number' ? cfg.dimming : 15,
        blur: typeof cfg.blur === 'number' ? cfg.blur : 0,
        volume: typeof cfg.volume === 'number' ? cfg.volume : 80,
        playbackRate: typeof cfg.playbackRate === 'number' ? cfg.playbackRate : 1.0,
        audioDucking: typeof cfg.audioDucking === 'boolean' ? cfg.audioDucking : true,
        interactive: typeof selectedWall.interactive === 'boolean' ? selectedWall.interactive : true
      });
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
    }
  }, [selectedWall?.id]);

  const isCurrentSelectedRunning = selectedWall ? isWallpaperRunningOnDesktop(selectedWall) : false;

  const handleUniversalChange = (key: keyof UniversalWallpaperSettings, val: any) => {
    setUniversal(prev => ({ ...prev, [key]: val }));
    postPropertyToPreview(key, val);
    onSendProperty(key, val, selectedWall?.id, selectedWall?.packagePath);
  };

  const handleCustomPropChange = (key: string, val: any) => {
    setCustomProps(prev => ({ ...prev, [key]: val }));
    postPropertyToPreview(key, val);
    onSendProperty(key, val, selectedWall?.id, selectedWall?.packagePath);
  };

  const handleResetDefaults = () => {
    setUniversal({
      volume: 80,
      audioDucking: true,
      dimming: 15,
      blur: 0,
      scaling: 'cover',
      playbackRate: 1.0,
      interactive: true
    });
    if (selectedWall?.properties) {
      const initial: Record<string, any> = {};
      Object.entries(selectedWall.properties).forEach(([key, val]) => {
        if (typeof val === 'object' && val !== null && 'value' in val) {
          initial[key] = val.value;
        }
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
        audioDucking: universal.audioDucking
      }
    });
  };

  const filteredWallpapers = wallpapers.filter(w => {
    if (!searchQuery.trim()) return true;
    const q = searchQuery.toLowerCase();
    return (
      w.name.toLowerCase().includes(q) ||
      (w.description && w.description.toLowerCase().includes(q)) ||
      w.id.toLowerCase().includes(q)
    );
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
      const file = e.dataTransfer.files[0];
      if (onImportMedia) {
        onImportMedia(file.name);
      }
    }
  };

  const showDragActive = isDraggingFile || isExternalDragging;
  const hasCustomProperties = selectedWall?.properties && Object.keys(selectedWall.properties).length > 0;

  return (
    <div className="relative space-y-4 animate-in fade-in duration-150">
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
            ? 'bg-emerald-50/70 border-emerald-300/90 shadow-2xs'
            : 'bg-white border-slate-200/80 shadow-2xs'
        }`}
      >
        <div className="flex items-center gap-3 min-w-0">
          <div
            className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0 border ${
              runningWall
                ? 'bg-emerald-600 text-white border-emerald-600 shadow-xs'
                : 'bg-slate-100 text-slate-400 border-slate-200'
            }`}
          >
            {runningWall ? <CheckCircle2 className="w-5 h-5" /> : <Sparkles className="w-4 h-4" />}
          </div>
          <div className="min-w-0">
            <div className="flex items-center gap-2">
              <span className="text-xs font-bold text-slate-900">
                {runningWall ? `当前桌面正在使用：${runningWall.name}` : '当前桌面未使用动态壁纸'}
              </span>
              {runningWall && (
                <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-bold bg-emerald-600 text-white">
                  <span className="w-1.5 h-1.5 rounded-full bg-white animate-pulse" />
                  运行中
                </span>
              )}
            </div>
            <p className="text-[11px] text-slate-500 truncate mt-0.5">
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
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-white hover:bg-rose-50 text-rose-600 border border-rose-200 transition-colors flex items-center gap-1.5 shadow-2xs cursor-pointer"
            >
              <Square className="w-3 h-3 fill-rose-500 text-rose-500" />
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
          {/* Top Control Bar */}
          <div className="bg-white rounded-2xl p-3.5 border border-slate-200/80 shadow-2xs flex items-center justify-between gap-3">
            <div className="relative flex-1">
              <Search className="w-4 h-4 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder="搜索壁纸..."
                className="w-full pl-9 pr-3 py-1.5 rounded-xl text-xs bg-slate-50 border border-slate-200/90 text-slate-800 focus:outline-none focus:ring-2 focus:ring-indigo-500/15 focus:border-indigo-500 transition-all placeholder:text-slate-400"
              />
            </div>

            <button
              type="button"
              onClick={() => {
                if (onImportMedia) onImportMedia('New_Wallpaper');
              }}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-700 text-white transition-all shadow-2xs flex items-center gap-1.5 shrink-0 active:scale-95 cursor-pointer"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>导入壁纸</span>
            </button>
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
                ? 'border-indigo-500 bg-indigo-50/80 scale-[1.005]'
                : 'border-slate-300/90 bg-white/80 hover:bg-indigo-50/30 hover:border-indigo-300'
            }`}
          >
            <div className="flex items-center justify-center gap-2 text-slate-600 text-xs font-medium">
              <Upload className={`w-4 h-4 ${showDragActive ? 'text-indigo-600 animate-bounce' : 'text-indigo-500'}`} />
              <span>将视频 (MP4/WebM) 或图片 (PNG/JPG/WebP) 拖入窗口，或点击此处选择文件制作壁纸</span>
            </div>
          </div>

          {/* Gallery Cards Grid */}
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
                      ? 'border-2 border-emerald-500 bg-emerald-50/25 shadow-sm ring-2 ring-emerald-500/15'
                      : isSelected
                      ? 'border-2 border-indigo-600 bg-white shadow-xs ring-2 ring-indigo-500/15'
                      : 'border border-slate-200/80 bg-white hover:border-slate-300 hover:shadow-2xs'
                  }`}
                >
                  {/* 16:9 Thumbnail */}
                  <div className="relative w-full aspect-video bg-black overflow-hidden select-none">
                    <iframe
                      src={getWallpaperPreviewUrl(wall)}
                      title={wall.name}
                      className="w-full h-full border-0 pointer-events-none select-none"
                      tabIndex={-1}
                      scrolling="no"
                    />

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
                        <span className="flex items-center gap-1 px-2 py-0.5 rounded-md bg-slate-900/75 backdrop-blur-xs text-white text-[10px] font-medium">
                          {wall.wallpaperType === 'video' ? (
                            <Film className="w-2.5 h-2.5 text-indigo-300" />
                          ) : (
                            <ImageIcon className="w-2.5 h-2.5 text-emerald-300" />
                          )}
                          自定义
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
                          className="p-1.5 rounded-lg bg-slate-900/75 hover:bg-indigo-600 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer"
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
                          className="p-1.5 rounded-lg bg-slate-900/75 hover:bg-slate-800 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer"
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
                            onDeleteWallpaper(wall.id, wall.packagePath, wall.name);
                          }}
                          className="p-1.5 rounded-lg bg-slate-900/75 hover:bg-rose-600 text-white backdrop-blur-xs transition-colors shadow-xs cursor-pointer"
                          title="删除此壁纸"
                        >
                          <Trash2 className="w-3.5 h-3.5" />
                        </button>
                      )}
                    </div>
                  </div>

                  {/* Card Footer with Direct Action Button */}
                  <div className="p-3 flex items-center justify-between gap-2 bg-white">
                    <div className="min-w-0">
                      <div className="flex items-center gap-1.5">
                        <h4 className="text-xs font-bold text-slate-900 truncate">{wall.name}</h4>
                      </div>
                      <p className="text-[11px] text-slate-500 line-clamp-1 mt-0.5">
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
                          className="px-2.5 py-1.5 rounded-lg text-[11px] font-bold bg-emerald-50 text-emerald-700 hover:bg-rose-50 hover:text-rose-600 border border-emerald-200 hover:border-rose-200 transition-colors shrink-0 flex items-center gap-1 cursor-pointer"
                          title="点击关闭动态壁纸"
                        >
                          <CheckCircle2 className="w-3.5 h-3.5" />
                          <span>使用中</span>
                        </button>
                      ) : (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            setSelectedWallId(wall.id);
                            onApplyWallpaper(wall.id, wall.packagePath);
                          }}
                          className="px-3 py-1.5 rounded-lg text-[11px] font-semibold bg-indigo-600 hover:bg-indigo-700 text-white transition-all shrink-0 flex items-center gap-1 shadow-2xs active:scale-95 cursor-pointer"
                        >
                          <Play className="w-3 h-3 fill-white" />
                          <span>设为壁纸</span>
                        </button>
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>

          {filteredWallpapers.length === 0 && (
            <div className="py-12 text-center bg-white rounded-2xl border border-slate-200/80">
              <ImageOff className="w-8 h-8 text-slate-300 mx-auto mb-2" />
              <p className="text-xs text-slate-500">未找到壁纸</p>
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
              <div className="bg-white rounded-2xl p-3.5 border border-slate-200/80 shadow-2xs flex flex-col gap-2.5">
                <div className="flex items-center justify-between gap-2">
                  <div className="flex items-center gap-2 min-w-0">
                    <Eye className="w-3.5 h-3.5 text-indigo-600 shrink-0" />
                    <span className="text-xs font-bold text-slate-900 truncate">{selectedWall.name}</span>
                  </div>
                  <div className="flex items-center gap-1.5 shrink-0">
                    {isCustomWallpaper(selectedWall) && onEditCustomWallpaper && (
                      <button
                        type="button"
                        onClick={() => handleOpenEditModal(selectedWall)}
                        className="px-2 py-0.5 rounded-md text-[10px] font-semibold bg-indigo-50 hover:bg-indigo-100 text-indigo-600 border border-indigo-200/70 flex items-center gap-1 transition-colors cursor-pointer"
                      >
                        <Settings2 className="w-3 h-3" />
                        <span>配置</span>
                      </button>
                    )}
                    {isCurrentSelectedRunning ? (
                      <span className="px-2 py-0.5 rounded-full text-[10px] font-bold bg-emerald-50 text-emerald-700 border border-emerald-200 shrink-0">
                        ● 当前桌面使用中
                      </span>
                    ) : (
                      <span className="px-2 py-0.5 rounded-full text-[10px] font-medium bg-slate-100 text-slate-600 shrink-0">
                        预览模式 · 未应用
                      </span>
                    )}
                  </div>
                </div>

                {/* Real 16:9 Canvas */}
                <div className="relative w-full aspect-video rounded-xl overflow-hidden bg-black border border-slate-900/60 select-none shadow-inner">
                  <iframe
                    ref={previewIframeRef}
                    src={getWallpaperPreviewUrl(selectedWall)}
                    title={selectedWall.name}
                    onLoad={syncAllPropsToPreview}
                    className="w-full h-full border-0 pointer-events-none select-none"
                    tabIndex={-1}
                    scrolling="no"
                    style={{
                      filter: universal.blur > 0 ? `blur(${universal.blur}px)` : 'none',
                      transform: universal.blur > 0 ? 'scale(1.04)' : 'scale(1)'
                    }}
                  />

                  {/* Dimming overlay if configured */}
                  {universal.dimming > 0 && (
                    <div
                      className="absolute inset-0 bg-black pointer-events-none transition-opacity duration-150"
                      style={{ opacity: universal.dimming / 100 }}
                    />
                  )}
                </div>
              </div>

              {/* Customizer Card */}
              <div className="bg-white rounded-2xl p-4 border border-slate-200/80 shadow-2xs space-y-4">
                {/* Dynamic Author Properties */}
                {hasCustomProperties && (
                  <div>
                    <div className="flex items-center gap-1.5 mb-2.5 pb-2 border-b border-slate-100">
                      <Sliders className="w-3.5 h-3.5 text-indigo-600" />
                      <span className="text-xs font-bold text-slate-900">壁纸个性设置</span>
                    </div>

                    <div className="space-y-3">
                      {Object.entries(selectedWall.properties!).map(([key, schema]: [string, any]) => {
                        const type = schema.type || 'slider';
                        const label = schema.text || key;
                        const val = customProps[key] !== undefined ? customProps[key] : schema.value;

                        return (
                          <div key={key} className="space-y-1">
                            <div className="flex items-center justify-between text-xs">
                              <span className="text-slate-700 font-medium">{label}</span>
                              <span className="text-[11px] font-mono text-slate-500">{String(val)}</span>
                            </div>

                            {type === 'slider' && (
                              <input
                                type="range"
                                min={schema.min ?? 0}
                                max={schema.max ?? 100}
                                step={schema.step ?? 1}
                                value={Number(val) || 0}
                                onChange={(e) => handleCustomPropChange(key, parseFloat(e.target.value))}
                                className="w-full h-1.5 bg-slate-100 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                              />
                            )}

                            {type === 'color' && (
                              <div className="flex items-center gap-2">
                                <input
                                  type="color"
                                  value={String(val || '#6366f1')}
                                  onChange={(e) => handleCustomPropChange(key, e.target.value)}
                                  className="w-7 h-7 rounded border border-slate-200 cursor-pointer"
                                />
                                <span className="text-xs font-mono text-slate-600">{String(val || '#6366f1')}</span>
                              </div>
                            )}

                            {type === 'boolean' && (
                              <button
                                type="button"
                                onClick={() => handleCustomPropChange(key, !val)}
                                className={`relative inline-flex h-4.5 w-8 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ${
                                  val ? 'bg-indigo-600' : 'bg-slate-300'
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

                {/* Host Display & Audio Controls */}
                <div>
                  <div className="flex items-center gap-1.5 mb-2.5 pb-2 border-b border-slate-100">
                    <span className="text-xs font-bold text-slate-900">播放与视觉效果</span>
                  </div>

                  <div className="space-y-3.5">
                    {/* Scaling Mode */}
                    <div className="space-y-1.5">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 font-medium flex items-center gap-1.5">
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
                                ? 'bg-indigo-600 text-white border-indigo-600'
                                : 'bg-slate-50 text-slate-600 border-slate-200 hover:bg-slate-100'
                            }`}
                          >
                            {item.label}
                          </button>
                        ))}
                      </div>
                    </div>

                    {/* Desktop Readability Dimming */}
                    <div className="space-y-1">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 font-medium flex items-center gap-1.5">
                          <Moon className="w-3.5 h-3.5 text-slate-400" />
                          壁纸压暗程度
                        </span>
                        <span className="text-[11px] font-mono text-slate-500">{universal.dimming}%</span>
                      </div>
                      <input
                        type="range"
                        min={0}
                        max={80}
                        value={universal.dimming}
                        onChange={(e) => handleUniversalChange('dimming', parseInt(e.target.value, 10))}
                        className="w-full h-1.5 bg-slate-100 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                      />
                    </div>

                    {/* Background Blur */}
                    <div className="space-y-1">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 font-medium flex items-center gap-1.5">
                          <Sparkles className="w-3.5 h-3.5 text-slate-400" />
                          背景模糊度
                        </span>
                        <span className="text-[11px] font-mono text-slate-500">{universal.blur} px</span>
                      </div>
                      <input
                        type="range"
                        min={0}
                        max={20}
                        value={universal.blur}
                        onChange={(e) => handleUniversalChange('blur', parseInt(e.target.value, 10))}
                        className="w-full h-1.5 bg-slate-100 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                      />
                    </div>

                    {/* Volume */}
                    <div className="space-y-1">
                      <div className="flex items-center justify-between text-xs">
                        <span className="text-slate-700 font-medium flex items-center gap-1.5">
                          <Volume2 className="w-3.5 h-3.5 text-slate-400" />
                          音量
                        </span>
                        <span className="text-[11px] font-mono text-slate-500">{universal.volume}%</span>
                      </div>
                      <input
                        type="range"
                        min={0}
                        max={100}
                        value={universal.volume}
                        onChange={(e) => handleUniversalChange('volume', parseInt(e.target.value, 10))}
                        className="w-full h-1.5 bg-slate-100 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                      />
                    </div>

                    {/* Playback Rate */}
                    {selectedWall.wallpaperType === 'video' && (
                      <div className="space-y-1.5">
                        <div className="flex items-center justify-between text-xs">
                          <span className="text-slate-700 font-medium flex items-center gap-1.5">
                            <Gauge className="w-3.5 h-3.5 text-slate-400" />
                            视频播放倍速
                          </span>
                          <span className="text-[11px] font-mono text-slate-500">{universal.playbackRate}x</span>
                        </div>
                        <div className="grid grid-cols-6 gap-1">
                          {[0.5, 0.75, 1.0, 1.25, 1.5, 2.0].map((rate) => (
                            <button
                              key={rate}
                              type="button"
                              onClick={() => handleUniversalChange('playbackRate', rate)}
                              className={`py-1 rounded-md text-[10px] font-bold border transition-all cursor-pointer ${
                                universal.playbackRate === rate
                                  ? 'bg-indigo-600 text-white border-indigo-600'
                                  : 'bg-slate-50 text-slate-600 border-slate-200 hover:bg-slate-100'
                              }`}
                            >
                              {rate}x
                            </button>
                          ))}
                        </div>
                      </div>
                    )}

                    {/* Audio Ducking */}
                    <label className="flex items-center justify-between p-2 rounded-xl bg-slate-50 border border-slate-100 cursor-pointer">
                      <span className="text-xs font-medium text-slate-700">其他软件发声时自动静音</span>
                      <input
                        type="checkbox"
                        checked={universal.audioDucking}
                        onChange={(e) => handleUniversalChange('audioDucking', e.target.checked)}
                        className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4"
                      />
                    </label>

                    {/* Interactive Toggle */}
                    <label className="flex items-center justify-between p-2 rounded-xl bg-slate-50 border border-slate-100 cursor-pointer">
                      <span className="text-xs font-medium text-slate-700">支持鼠标互动效果</span>
                      <input
                        type="checkbox"
                        checked={universal.interactive}
                        onChange={(e) => handleUniversalChange('interactive', e.target.checked)}
                        className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-4 h-4"
                      />
                    </label>
                  </div>
                </div>

                {/* Action Buttons */}
                <div className="pt-2 border-t border-slate-100 flex items-center gap-2">
                  {isCurrentSelectedRunning ? (
                    <button
                      type="button"
                      onClick={onStopWallpaper}
                      className="flex-1 py-2.5 px-4 rounded-xl text-xs font-bold bg-rose-50 text-rose-600 border border-rose-200 hover:bg-rose-100 transition-colors flex items-center justify-center gap-2 shadow-2xs cursor-pointer"
                    >
                      <Square className="w-3.5 h-3.5 text-rose-500 fill-rose-500" />
                      <span>关闭壁纸</span>
                    </button>
                  ) : (
                    <button
                      type="button"
                      onClick={() => onApplyWallpaper(selectedWall.id, selectedWall.packagePath)}
                      className="flex-1 py-2.5 px-4 rounded-xl text-xs font-bold bg-indigo-600 hover:bg-indigo-700 text-white transition-all shadow-md shadow-indigo-600/20 active:scale-[0.98] flex items-center justify-center gap-2 cursor-pointer"
                    >
                      <Play className="w-3.5 h-3.5 fill-white" />
                      <span>设为当前桌面壁纸</span>
                    </button>
                  )}

                  {isCustomWallpaper(selectedWall) && onDeleteWallpaper && (
                    <button
                      type="button"
                      onClick={() => onDeleteWallpaper(selectedWall.id, selectedWall.packagePath, selectedWall.name)}
                      className="p-2.5 rounded-xl text-xs font-medium text-rose-500 hover:text-rose-700 hover:bg-rose-50 transition-colors cursor-pointer"
                      title="删除此自定义壁纸"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  )}

                  <button
                    type="button"
                    onClick={handleResetDefaults}
                    className="p-2.5 rounded-xl text-xs font-medium text-slate-500 hover:text-slate-800 hover:bg-slate-100 transition-colors cursor-pointer"
                    title="恢复默认参数"
                  >
                    <RefreshCw className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>
            </>
          ) : (
            <div className="py-24 text-center bg-white rounded-2xl border border-slate-200/80">
              <ImageOff className="w-8 h-8 text-slate-300 mx-auto mb-2" />
              <p className="text-xs text-slate-500">请选择左侧壁纸</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
