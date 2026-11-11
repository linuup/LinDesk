import React, { useState, useEffect, useRef } from 'react';
import { StagedWallpaperMedia } from '../types';
import { 
  X, Check, Play, Film, Image as ImageIcon, Volume2, 
  VolumeX, Moon, Sparkles, Maximize2, Gauge, Save
} from 'lucide-react';

interface WallpaperImportModalProps {
  media: StagedWallpaperMedia | null;
  onClose: () => void;
  onConfirm: (config: {
    filePath: string;
    name: string;
    description: string;
    scaling: 'cover' | 'contain' | 'fill';
    dimming: number;
    blur: number;
    volume: number;
    muted: boolean;
    playbackRate: number;
    audioDucking: boolean;
    applyImmediately: boolean;
    editWallpaperId?: string;
    packagePath?: string;
  }) => void;
}

const PLAYBACK_RATES = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0];

export const WallpaperImportModal: React.FC<WallpaperImportModalProps> = ({
  media,
  onClose,
  onConfirm
}) => {
  if (!media) return null;

  const isVideo = media.wallpaperType === 'video';
  const isNewlyStaged = Boolean(media.isNewlyStaged);
  const isEditing = Boolean(media.editWallpaperId) && !isNewlyStaged;

  const [name, setName] = useState(media.name || '未命名壁纸');
  const [description, setDescription] = useState(media.description || '');
  const [scaling, setScaling] = useState<'cover' | 'contain' | 'fill'>(
    media.customConfig?.scaling || 'cover'
  );
  const [dimming, setDimming] = useState(media.customConfig?.dimming ?? 0);
  const [blur, setBlur] = useState(() => {
    const raw = media.customConfig?.blur ?? 0;
    return raw > 0 && raw <= 20 ? Math.round((raw / 20) * 100) : raw;
  });
  const [volume, setVolume] = useState(media.customConfig?.volume ?? 80);
  const [muted, setMuted] = useState(media.customConfig?.muted ?? false);
  const [playbackRate, setPlaybackRate] = useState(media.customConfig?.playbackRate ?? 1.0);
  const [audioDucking, setAudioDucking] = useState(media.customConfig?.audioDucking ?? true);
  const [showDesktopIcons, setShowDesktopIcons] = useState(false);

  const videoRef = useRef<HTMLVideoElement | null>(null);

  // ESC to close
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        onClose();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [onClose]);

  // Sync state when new media is opened
  useEffect(() => {
    setName(media.name || '未命名壁纸');
    setDescription(media.description || '');
    setScaling(media.customConfig?.scaling || 'cover');
    setDimming(media.customConfig?.dimming ?? 0);
    const rawBlur = media.customConfig?.blur ?? 0;
    setBlur(rawBlur > 0 && rawBlur <= 20 ? Math.round((rawBlur / 20) * 100) : rawBlur);
    setVolume(media.customConfig?.volume ?? 80);
    setMuted(media.customConfig?.muted ?? false);
    setPlaybackRate(media.customConfig?.playbackRate ?? 1.0);
    setAudioDucking(media.customConfig?.audioDucking ?? true);
  }, [media]);

  // Sync real-time video playback parameters in preview
  useEffect(() => {
    if (videoRef.current) {
      videoRef.current.playbackRate = playbackRate;
      videoRef.current.muted = muted;
      videoRef.current.volume = muted ? 0 : volume / 100;
    }
  }, [playbackRate, volume, muted]);

  const formatFileSize = (bytes?: number) => {
    if (!bytes || bytes <= 0) return '';
    const mb = bytes / (1024 * 1024);
    if (mb < 1) {
      return `${(bytes / 1024).toFixed(0)} KB`;
    }
    return `${mb.toFixed(1)} MB`;
  };

  const handleSubmit = (applyImmediately: boolean) => {
    onConfirm({
      filePath: media.filePath,
      name: name.trim() || media.name || '未命名壁纸',
      description: description.trim(),
      scaling,
      dimming,
      blur,
      volume,
      muted,
      playbackRate,
      audioDucking,
      applyImmediately,
      editWallpaperId: media.editWallpaperId,
      packagePath: media.packagePath
    });
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 dark:bg-black/75 backdrop-blur-sm p-4 animate-in fade-in duration-150 select-none"
      onClick={onClose}
    >
      <div
        className="bg-white dark:bg-[#131927] rounded-2xl border border-slate-200 dark:border-slate-800/90 shadow-2xl w-full max-w-3xl overflow-hidden flex flex-col max-h-[90vh]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="px-5 py-3.5 border-b border-slate-200/80 dark:border-slate-800/80 flex items-center justify-between bg-slate-50/80 dark:bg-[#0f1422]">
          <div className="flex items-center gap-2.5">
            <div className={`w-8 h-8 rounded-xl flex items-center justify-center shadow-xs text-white ${
              isNewlyStaged ? 'bg-emerald-600 dark:bg-emerald-500' : 'bg-indigo-600'
            }`}>
              {isVideo ? <Film className="w-4 h-4" /> : <ImageIcon className="w-4 h-4" />}
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h3 className="text-sm font-bold text-slate-900 dark:text-slate-100">
                  {isNewlyStaged ? '确认导入新壁纸 · 预览与微调' : isEditing ? '编辑壁纸效果与参数' : '导入壁纸设置'}
                </h3>
                <span className={`px-2 py-0.5 rounded-md text-[10px] font-bold ${
                  isNewlyStaged 
                    ? 'bg-emerald-50 dark:bg-emerald-950/80 text-emerald-700 dark:text-emerald-300 border border-emerald-200 dark:border-emerald-800/60'
                    : 'bg-indigo-50 dark:bg-indigo-950/80 text-indigo-700 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-800/60'
                }`}>
                  {isNewlyStaged 
                    ? (isVideo ? `拖拽新视频 · ${media.ext || 'MP4'}` : `拖拽新图片 · ${media.ext || 'IMG'}`)
                    : (isVideo ? `视频 · ${media.ext || 'MP4'}` : `图片 · ${media.ext || 'IMG'}`)}
                </span>
                {media.sizeBytes ? (
                  <span className="text-[11px] text-slate-500 dark:text-slate-400 font-mono">
                    {formatFileSize(media.sizeBytes)}
                  </span>
                ) : null}
              </div>
              <p className="text-[11px] text-slate-500 dark:text-slate-400">
                {isNewlyStaged 
                  ? '已检测到新拖入的文件，请在导入前核对名称及画面填充、压暗、音量等参数'
                  : '实时预览并微调画面比例、压暗程度、高斯模糊及声音表现'}
              </p>
            </div>
          </div>

          <button
            type="button"
            onClick={onClose}
            className="w-7 h-7 rounded-lg text-slate-400 hover:text-slate-700 dark:hover:text-white hover:bg-slate-100 dark:hover:bg-slate-800 flex items-center justify-center transition-colors cursor-pointer"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Body */}
        <div className="p-5 overflow-y-auto grid grid-cols-1 md:grid-cols-12 gap-5">
          {/* Staging Callout Notice for Dragged-in Wallpapers */}
          {isNewlyStaged && (
            <div className="md:col-span-12 p-3 rounded-xl bg-emerald-50/80 dark:bg-emerald-950/40 border border-emerald-200/80 dark:border-emerald-800/60 flex items-center justify-between text-xs text-emerald-900 dark:text-emerald-200 animate-in fade-in duration-150">
              <div className="flex items-center gap-2">
                <Sparkles className="w-4 h-4 text-emerald-600 dark:text-emerald-400 shrink-0" />
                <span className="font-medium">
                  已为您成功载入拖入的媒体文件！请在下方预览效果并调整参数，确认无误后存入画廊或立即设为壁纸。
                </span>
              </div>
              {media.filePath && (
                <span className="text-[11px] text-emerald-700 dark:text-emerald-300 font-mono truncate max-w-[200px] shrink-0" title={media.filePath}>
                  {media.filePath.split(/[/\\]/).pop()}
                </span>
              )}
            </div>
          )}

          {/* Left 7 Cols: Live 16:9 Preview + Basic Info */}
          <div className="md:col-span-7 flex flex-col gap-4">
            {/* 16:9 Live Preview Box */}
            <div className="space-y-1.5">
              <div className="flex items-center justify-between text-xs">
                <span className="font-bold text-slate-700 dark:text-slate-300 flex items-center gap-1.5">
                  <Sparkles className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                  16:9 桌面效果实时预览
                </span>
                <span className="text-[11px] text-slate-500 dark:text-slate-400">
                  缩放：{scaling === 'cover' ? '铺满裁切' : scaling === 'contain' ? '完整适应' : '拉伸填满'}
                </span>
              </div>

              <div className="relative w-full aspect-video rounded-xl overflow-hidden bg-black border border-slate-300 dark:border-slate-800 shadow-inner select-none">
                {isVideo ? (
                  <video
                    ref={videoRef}
                    src={media.fileUrl}
                    autoPlay
                    loop
                    playsInline
                    className="w-full h-full pointer-events-none transition-all duration-150"
                    style={{
                      objectFit: scaling,
                      filter: blur > 0 ? `blur(${((blur > 20 ? (blur / 100) * 20 : blur)).toFixed(1)}px)` : 'none',
                      transform: blur > 0 ? 'scale(1.05)' : 'scale(1)'
                    }}
                  />
                ) : (
                  <img
                    src={media.fileUrl}
                    alt={name}
                    className="w-full h-full pointer-events-none transition-all duration-150"
                    style={{
                      objectFit: scaling,
                      filter: blur > 0 ? `blur(${((blur > 20 ? (blur / 100) * 20 : blur)).toFixed(1)}px)` : 'none',
                      transform: blur > 0 ? 'scale(1.05)' : 'scale(1)'
                    }}
                  />
                )}

                {/* Desktop Readability Dimming Mask */}
                {dimming > 0 && (
                  <div
                    className="absolute inset-0 bg-black pointer-events-none transition-opacity duration-150"
                    style={{ opacity: dimming / 100 }}
                  />
                )}

                {/* Simulated Desktop Icons Overlay */}
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

                {/* Preview Overlay Badge */}
                <div className="absolute bottom-2 right-2 flex items-center gap-2">
                  <button
                    type="button"
                    onClick={() => setShowDesktopIcons(!showDesktopIcons)}
                    className="px-2 py-0.5 rounded-md text-[10px] font-semibold bg-black/60 hover:bg-black/80 text-white backdrop-blur-xs transition-colors cursor-pointer border border-white/10"
                  >
                    {showDesktopIcons ? '隐藏桌面图标' : '模拟桌面图标'}
                  </button>
                </div>
              </div>
            </div>

            {/* Wallpaper Name & Description */}
            <div className="grid grid-cols-1 gap-3 bg-slate-50 dark:bg-slate-900 p-3.5 rounded-xl border border-slate-200 dark:border-slate-800">
              <div>
                <label className="block text-[11px] font-bold text-slate-700 dark:text-slate-300 mb-1">
                  壁纸名称
                </label>
                <input
                  type="text"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  placeholder="输入壁纸显示名称..."
                  className="w-full px-3 py-1.5 rounded-lg text-xs bg-white dark:bg-slate-950 border border-slate-200 dark:border-slate-800 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500 shadow-2xs"
                />
              </div>
              <div>
                <label className="block text-[11px] font-bold text-slate-700 dark:text-slate-300 mb-1">
                  备注描述（可选）
                </label>
                <input
                  type="text"
                  value={description}
                  onChange={(e) => setDescription(e.target.value)}
                  placeholder="输入简短描述..."
                  className="w-full px-3 py-1.5 rounded-lg text-xs bg-white dark:bg-slate-950 border border-slate-200 dark:border-slate-800 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500 shadow-2xs"
                />
              </div>
            </div>
          </div>

          {/* Right 5 Cols: Visual & Audio Parameters */}
          <div className="md:col-span-5 flex flex-col justify-between gap-4 bg-slate-50 dark:bg-slate-900 p-4 rounded-xl border border-slate-200 dark:border-slate-800">
            <div className="space-y-4">
              {/* 1. Scaling Mode */}
              <div className="space-y-1.5">
                <span className="text-xs font-bold text-slate-800 dark:text-slate-200 flex items-center gap-1.5">
                  <Maximize2 className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                  画面填充模式
                </span>
                <div className="grid grid-cols-3 gap-1.5">
                  {([
                    { key: 'cover', label: '铺满裁切' },
                    { key: 'contain', label: '完整适应' },
                    { key: 'fill', label: '拉伸填满' }
                  ] as const).map((item) => (
                    <button
                      key={item.key}
                      type="button"
                      onClick={() => setScaling(item.key)}
                      className={`py-1.5 px-2 rounded-lg text-[11px] font-semibold border transition-all cursor-pointer ${
                        scaling === item.key
                          ? 'bg-indigo-600 text-white border-indigo-600 shadow-xs'
                          : 'bg-white dark:bg-slate-950 text-slate-700 dark:text-slate-400 border-slate-200 dark:border-slate-800 hover:bg-slate-100 dark:hover:border-slate-700 shadow-2xs'
                      }`}
                    >
                      {item.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* 2. Dimming */}
              <div className="space-y-1">
                <div className="flex items-center justify-between text-xs">
                  <span className="font-bold text-slate-800 dark:text-slate-200 flex items-center gap-1.5">
                    <Moon className="w-3.5 h-3.5 text-slate-500 dark:text-slate-400" />
                    桌面压暗程度
                  </span>
                  <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{dimming}%</span>
                </div>
                <input
                  type="range"
                  min={0}
                  max={80}
                  value={dimming}
                  onChange={(e) => setDimming(parseInt(e.target.value, 10))}
                  className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                />
              </div>

              {/* 3. Blur */}
              <div className="space-y-1">
                <div className="flex items-center justify-between text-xs">
                  <span className="font-bold text-slate-800 dark:text-slate-200 flex items-center gap-1.5">
                    <Sparkles className="w-3.5 h-3.5 text-slate-500 dark:text-slate-400" />
                    背景模糊度
                  </span>
                  <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{blur}%</span>
                </div>
                <input
                  type="range"
                  min={0}
                  max={100}
                  step={1}
                  value={blur}
                  onChange={(e) => setBlur(parseInt(e.target.value, 10))}
                  className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500"
                />
              </div>

              {/* 4. Volume & Audio Ducking (Only for videos) */}
              {isVideo && (
                <div className="space-y-3.5 pt-3 border-t border-slate-200/80 dark:border-slate-800/80">
                  <div className="space-y-1">
                    <div className="flex items-center justify-between text-xs">
                      <span className="font-bold text-slate-800 dark:text-slate-200 flex items-center gap-1.5">
                        {muted || volume === 0 ? (
                          <VolumeX className="w-3.5 h-3.5 text-rose-500 dark:text-rose-400" />
                        ) : (
                          <Volume2 className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
                        )}
                        初始音量
                      </span>
                      <div className="flex items-center gap-2">
                        <button
                          type="button"
                          onClick={() => setMuted(!muted)}
                          className={`px-1.5 py-0.5 rounded text-[10px] font-bold cursor-pointer transition-colors ${
                            muted
                              ? 'bg-rose-50 dark:bg-rose-950/80 text-rose-700 dark:text-rose-300 border border-rose-200 dark:border-rose-800/60'
                              : 'bg-white dark:bg-slate-800 text-slate-600 dark:text-slate-400 hover:bg-slate-100 dark:hover:bg-slate-700 border border-slate-200 dark:border-slate-700 shadow-2xs'
                          }`}
                        >
                          {muted ? '已静音' : '静音'}
                        </button>
                        <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">
                          {muted ? '0%' : `${volume}%`}
                        </span>
                      </div>
                    </div>
                    <input
                      type="range"
                      min={0}
                      max={100}
                      disabled={muted}
                      value={muted ? 0 : volume}
                      onChange={(e) => {
                        if (muted) setMuted(false);
                        setVolume(parseInt(e.target.value, 10));
                      }}
                      className="w-full h-1.5 bg-slate-200 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-indigo-600 dark:accent-indigo-500 disabled:opacity-40"
                    />
                  </div>

                  {/* Playback Rate */}
                  <div className="space-y-1.5">
                    <div className="flex items-center justify-between text-xs">
                      <span className="font-bold text-slate-800 dark:text-slate-200 flex items-center gap-1.5">
                        <Gauge className="w-3.5 h-3.5 text-slate-500 dark:text-slate-400" />
                        视频播放速度
                      </span>
                      <span className="text-[11px] font-mono text-slate-500 dark:text-slate-400">{playbackRate}x</span>
                    </div>
                    <div className="grid grid-cols-6 gap-1">
                      {PLAYBACK_RATES.map((rate) => (
                        <button
                          key={rate}
                          type="button"
                          onClick={() => setPlaybackRate(rate)}
                          className={`py-1 rounded-md text-[10px] font-bold border transition-all cursor-pointer ${
                            playbackRate === rate
                              ? 'bg-indigo-600 text-white border-indigo-600 shadow-xs'
                              : 'bg-white dark:bg-slate-950 text-slate-700 dark:text-slate-400 border-slate-200 dark:border-slate-800 hover:bg-slate-100 dark:hover:border-slate-700 shadow-2xs'
                          }`}
                        >
                          {rate}x
                        </button>
                      ))}
                    </div>
                  </div>

                  {/* Audio Ducking */}
                  <label className="flex items-center justify-between p-2 rounded-lg bg-white dark:bg-slate-950 border border-slate-200 dark:border-slate-800 cursor-pointer shadow-2xs">
                    <span className="text-[11px] font-medium text-slate-700 dark:text-slate-300">
                      其他软件播放音频时自动静音
                    </span>
                    <input
                      type="checkbox"
                      checked={audioDucking}
                      onChange={(e) => setAudioDucking(e.target.checked)}
                      className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-3.5 h-3.5 bg-slate-100 dark:bg-slate-800 border-slate-300 dark:border-slate-700"
                    />
                  </label>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* Footer Actions */}
        <div className="px-5 py-3.5 border-t border-slate-200/80 dark:border-slate-800/80 bg-slate-50/80 dark:bg-[#0f1422] flex flex-wrap items-center justify-between gap-2">
          <button
            type="button"
            onClick={onClose}
            className="px-3.5 py-2 rounded-xl text-xs font-semibold text-slate-500 hover:text-slate-800 dark:text-slate-400 dark:hover:text-white hover:bg-slate-100 dark:hover:bg-slate-800 transition-colors cursor-pointer"
          >
            {isNewlyStaged ? '放弃导入' : '取消'}
          </button>

          <div className="flex items-center gap-2.5">
            <button
              type="button"
              onClick={() => handleSubmit(false)}
              className="px-4 py-2 rounded-xl text-xs font-semibold bg-white dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 text-slate-700 dark:text-slate-200 border border-slate-200 dark:border-slate-800 shadow-2xs transition-all flex items-center gap-1.5 cursor-pointer"
            >
              <Save className="w-3.5 h-3.5 text-slate-500 dark:text-slate-400" />
              <span>{isNewlyStaged ? '仅存入壁纸库' : isEditing ? '仅保存修改' : '仅保存到壁纸库'}</span>
            </button>

            <button
              type="button"
              onClick={() => handleSubmit(true)}
              className="px-4 py-2 rounded-xl text-xs font-bold bg-indigo-600 hover:bg-indigo-500 text-white shadow-md shadow-indigo-600/30 transition-all flex items-center gap-1.5 active:scale-95 cursor-pointer"
            >
              {isNewlyStaged ? (
                <>
                  <Play className="w-3.5 h-3.5 fill-white" />
                  <span>确认导入并立即设为壁纸</span>
                </>
              ) : isEditing ? (
                <>
                  <Check className="w-3.5 h-3.5" />
                  <span>保存并应用到桌面</span>
                </>
              ) : (
                <>
                  <Play className="w-3.5 h-3.5 fill-white" />
                  <span>保存并立即设为壁纸</span>
                </>
              )}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
