import React, { useState, useEffect, useRef } from 'react';
import {
  X, Play, Save, Film, Image as ImageIcon, Volume2, VolumeX,
  Moon, Sparkles, Maximize2, Gauge, Check
} from 'lucide-react';
import { StagedWallpaperMedia, CustomWallpaperConfig } from '../types';

interface WallpaperImportModalProps {
  media: StagedWallpaperMedia | null;
  onClose: () => void;
  onConfirm: (payload: {
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

const formatFileSize = (bytes?: number): string => {
  if (!bytes || bytes <= 0) return '';
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
};

const PLAYBACK_RATES = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0];

export const WallpaperImportModal: React.FC<WallpaperImportModalProps> = ({
  media,
  onClose,
  onConfirm
}) => {
  const videoRef = useRef<HTMLVideoElement | null>(null);

  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [scaling, setScaling] = useState<'cover' | 'contain' | 'fill'>('cover');
  const [dimming, setDimming] = useState(15);
  const [blur, setBlur] = useState(0);
  const [volume, setVolume] = useState(80);
  const [muted, setMuted] = useState(false);
  const [playbackRate, setPlaybackRate] = useState(1.0);
  const [audioDucking, setAudioDucking] = useState(true);

  useEffect(() => {
    if (!media) return;
    const cfg: CustomWallpaperConfig = media.customConfig || {};
    setName(media.name || '自定义壁纸');
    setDescription(
      media.description ||
        (media.wallpaperType === 'video' ? '本地导入的视频动态壁纸' : '本地导入的高清图片壁纸')
    );
    setScaling(cfg.scaling || 'cover');
    setDimming(typeof cfg.dimming === 'number' ? cfg.dimming : 15);
    setBlur(typeof cfg.blur === 'number' ? cfg.blur : 0);
    setVolume(typeof cfg.volume === 'number' ? cfg.volume : 80);
    setMuted(typeof cfg.muted === 'boolean' ? cfg.muted : false);
    setPlaybackRate(typeof cfg.playbackRate === 'number' ? cfg.playbackRate : 1.0);
    setAudioDucking(typeof cfg.audioDucking === 'boolean' ? cfg.audioDucking : true);
  }, [media]);

  useEffect(() => {
    if (videoRef.current) {
      videoRef.current.volume = muted ? 0 : Math.max(0, Math.min(1, volume / 100));
      videoRef.current.muted = muted || volume <= 0;
      videoRef.current.playbackRate = playbackRate;
    }
  }, [volume, muted, playbackRate, media]);

  if (!media) return null;

  const isVideo = media.wallpaperType === 'video';
  const isEditing = Boolean(media.editWallpaperId);

  const handleSubmit = (applyImmediately: boolean) => {
    onConfirm({
      filePath: media.filePath,
      name: name.trim() || media.name || '自定义壁纸',
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
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-slate-900/50 backdrop-blur-xs p-4 animate-in fade-in duration-150">
      <div
        className="bg-white rounded-2xl border border-slate-200/90 shadow-2xl w-full max-w-3xl overflow-hidden flex flex-col max-h-[90vh]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="px-5 py-3.5 border-b border-slate-100 flex items-center justify-between bg-slate-50/70">
          <div className="flex items-center gap-2.5">
            <div className="w-8 h-8 rounded-xl bg-indigo-600 text-white flex items-center justify-center shadow-xs">
              {isVideo ? <Film className="w-4 h-4" /> : <ImageIcon className="w-4 h-4" />}
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h3 className="text-sm font-bold text-slate-900">
                  {isEditing ? '编辑壁纸效果' : '导入壁纸设置'}
                </h3>
                <span className="px-2 py-0.5 rounded-md text-[10px] font-bold uppercase bg-indigo-50 text-indigo-600 border border-indigo-200/60">
                  {isVideo ? `视频 · ${media.ext || 'MP4'}` : `图片 · ${media.ext || 'IMG'}`}
                </span>
                {media.sizeBytes ? (
                  <span className="text-[11px] text-slate-400 font-mono">
                    {formatFileSize(media.sizeBytes)}
                  </span>
                ) : null}
              </div>
              <p className="text-[11px] text-slate-500">
                实时预览并调整画面比例、压暗程度、模糊度与音量
              </p>
            </div>
          </div>

          <button
            type="button"
            onClick={onClose}
            className="w-7 h-7 rounded-lg text-slate-400 hover:text-slate-700 hover:bg-slate-200/60 flex items-center justify-center transition-colors cursor-pointer"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Body */}
        <div className="p-5 overflow-y-auto grid grid-cols-1 md:grid-cols-12 gap-5">
          {/* Left 7 Cols: Live 16:9 Preview + Basic Info */}
          <div className="md:col-span-7 flex flex-col gap-4">
            {/* 16:9 Live Preview Box */}
            <div className="space-y-1.5">
              <div className="flex items-center justify-between text-xs">
                <span className="font-bold text-slate-700 flex items-center gap-1.5">
                  <Sparkles className="w-3.5 h-3.5 text-indigo-600" />
                  16:9 桌面效果实时预览
                </span>
                <span className="text-[11px] text-slate-400">
                  缩放：{scaling === 'cover' ? '铺满裁切' : scaling === 'contain' ? '完整适应' : '拉伸填满'}
                </span>
              </div>

              <div className="relative w-full aspect-video rounded-xl overflow-hidden bg-black border border-slate-900/80 shadow-inner select-none">
                {isVideo ? (
                  <video
                    ref={videoRef}
                    src={media.fileUrl}
                    autoPlay
                    loop
                    playsInline
                    className="w-full h-full transition-all duration-150"
                    style={{
                      objectFit: scaling,
                      filter: blur > 0 ? `blur(${blur}px)` : 'none',
                      transform: blur > 0 ? 'scale(1.04)' : 'scale(1)'
                    }}
                  />
                ) : (
                  <img
                    src={media.fileUrl}
                    alt={name}
                    className="w-full h-full transition-all duration-150"
                    style={{
                      objectFit: scaling,
                      filter: blur > 0 ? `blur(${blur}px)` : 'none',
                      transform: blur > 0 ? 'scale(1.04)' : 'scale(1)'
                    }}
                  />
                )}

                {/* Dimming Overlay */}
                <div
                  className="absolute inset-0 bg-black pointer-events-none transition-opacity duration-150"
                  style={{ opacity: dimming / 100 }}
                />

                {/* Simulated Desktop Icons Overlay for Contrast Preview */}
                <div className="absolute top-3 left-3 flex flex-col gap-2.5 pointer-events-none opacity-90">
                  <div className="flex flex-col items-center gap-1 w-12">
                    <div className="w-7 h-7 rounded-lg bg-white/20 backdrop-blur-xs border border-white/30 shadow-xs" />
                    <span className="text-[9px] text-white font-medium drop-shadow-xs truncate w-full text-center">
                      此电脑
                    </span>
                  </div>
                  <div className="flex flex-col items-center gap-1 w-12">
                    <div className="w-7 h-7 rounded-lg bg-indigo-500/30 backdrop-blur-xs border border-white/30 shadow-xs" />
                    <span className="text-[9px] text-white font-medium drop-shadow-xs truncate w-full text-center">
                      回收站
                    </span>
                  </div>
                </div>
              </div>
            </div>

            {/* Wallpaper Name & Description */}
            <div className="grid grid-cols-1 gap-3 bg-slate-50/70 p-3.5 rounded-xl border border-slate-200/70">
              <div>
                <label className="block text-[11px] font-bold text-slate-700 mb-1">
                  壁纸名称
                </label>
                <input
                  type="text"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  placeholder="输入壁纸显示名称..."
                  className="w-full px-3 py-1.5 rounded-lg text-xs bg-white border border-slate-200 text-slate-800 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500"
                />
              </div>
              <div>
                <label className="block text-[11px] font-bold text-slate-700 mb-1">
                  备注描述（可选）
                </label>
                <input
                  type="text"
                  value={description}
                  onChange={(e) => setDescription(e.target.value)}
                  placeholder="输入简短描述..."
                  className="w-full px-3 py-1.5 rounded-lg text-xs bg-white border border-slate-200 text-slate-800 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500"
                />
              </div>
            </div>
          </div>

          {/* Right 5 Cols: Visual & Audio Parameters */}
          <div className="md:col-span-5 flex flex-col justify-between gap-4 bg-slate-50/60 p-4 rounded-xl border border-slate-200/80">
            <div className="space-y-4">
              {/* 1. Scaling Mode */}
              <div className="space-y-1.5">
                <span className="text-xs font-bold text-slate-800 flex items-center gap-1.5">
                  <Maximize2 className="w-3.5 h-3.5 text-indigo-600" />
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
                          ? 'bg-indigo-600 text-white border-indigo-600 shadow-2xs'
                          : 'bg-white text-slate-600 border-slate-200 hover:border-slate-300'
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
                  <span className="text-slate-700 font-semibold flex items-center gap-1.5">
                    <Moon className="w-3.5 h-3.5 text-slate-400" />
                    壁纸压暗程度
                  </span>
                  <span className="text-[11px] font-mono text-slate-500">{dimming}%</span>
                </div>
                <input
                  type="range"
                  min={0}
                  max={80}
                  value={dimming}
                  onChange={(e) => setDimming(parseInt(e.target.value, 10))}
                  className="w-full h-1.5 bg-slate-200 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                />
              </div>

              {/* 3. Blur */}
              <div className="space-y-1">
                <div className="flex items-center justify-between text-xs">
                  <span className="text-slate-700 font-semibold flex items-center gap-1.5">
                    <Sparkles className="w-3.5 h-3.5 text-slate-400" />
                    背景模糊度
                  </span>
                  <span className="text-[11px] font-mono text-slate-500">{blur} px</span>
                </div>
                <input
                  type="range"
                  min={0}
                  max={20}
                  value={blur}
                  onChange={(e) => setBlur(parseInt(e.target.value, 10))}
                  className="w-full h-1.5 bg-slate-200 rounded-lg appearance-none cursor-pointer accent-indigo-600"
                />
              </div>

              {/* 4. Video Specific Controls */}
              {isVideo && (
                <div className="pt-3 border-t border-slate-200/80 space-y-3.5">
                  {/* Volume & Mute */}
                  <div className="space-y-1">
                    <div className="flex items-center justify-between text-xs">
                      <span className="text-slate-700 font-semibold flex items-center gap-1.5">
                        {muted || volume === 0 ? (
                          <VolumeX className="w-3.5 h-3.5 text-rose-500" />
                        ) : (
                          <Volume2 className="w-3.5 h-3.5 text-indigo-600" />
                        )}
                        默认音量
                      </span>
                      <div className="flex items-center gap-2">
                        <button
                          type="button"
                          onClick={() => setMuted(!muted)}
                          className={`px-1.5 py-0.5 rounded text-[10px] font-bold cursor-pointer ${
                            muted
                              ? 'bg-rose-100 text-rose-600'
                              : 'bg-slate-200/70 text-slate-600 hover:bg-slate-200'
                          }`}
                        >
                          {muted ? '已静音' : '静音'}
                        </button>
                        <span className="text-[11px] font-mono text-slate-500">
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
                        setMuted(false);
                        setVolume(parseInt(e.target.value, 10));
                      }}
                      className="w-full h-1.5 bg-slate-200 rounded-lg appearance-none cursor-pointer accent-indigo-600 disabled:opacity-50"
                    />
                  </div>

                  {/* Playback Speed */}
                  <div className="space-y-1.5">
                    <div className="flex items-center justify-between text-xs">
                      <span className="text-slate-700 font-semibold flex items-center gap-1.5">
                        <Gauge className="w-3.5 h-3.5 text-slate-400" />
                        播放倍速
                      </span>
                      <span className="text-[11px] font-mono text-slate-500">{playbackRate}x</span>
                    </div>
                    <div className="grid grid-cols-6 gap-1">
                      {PLAYBACK_RATES.map((rate) => (
                        <button
                          key={rate}
                          type="button"
                          onClick={() => setPlaybackRate(rate)}
                          className={`py-1 rounded-md text-[10px] font-bold border transition-all cursor-pointer ${
                            playbackRate === rate
                              ? 'bg-indigo-600 text-white border-indigo-600'
                              : 'bg-white text-slate-600 border-slate-200 hover:border-slate-300'
                          }`}
                        >
                          {rate}x
                        </button>
                      ))}
                    </div>
                  </div>

                  {/* Audio Ducking */}
                  <label className="flex items-center justify-between p-2 rounded-lg bg-white border border-slate-200/80 cursor-pointer">
                    <span className="text-[11px] font-medium text-slate-700">
                      其他软件播放音频时自动静音
                    </span>
                    <input
                      type="checkbox"
                      checked={audioDucking}
                      onChange={(e) => setAudioDucking(e.target.checked)}
                      className="rounded text-indigo-600 focus:ring-0 cursor-pointer w-3.5 h-3.5"
                    />
                  </label>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* Footer Actions */}
        <div className="px-5 py-3.5 border-t border-slate-100 bg-slate-50/70 flex flex-wrap items-center justify-between gap-2">
          <button
            type="button"
            onClick={onClose}
            className="px-3.5 py-2 rounded-xl text-xs font-semibold text-slate-600 hover:bg-slate-200/60 transition-colors cursor-pointer"
          >
            取消
          </button>

          <div className="flex items-center gap-2.5">
            <button
              type="button"
              onClick={() => handleSubmit(false)}
              className="px-4 py-2 rounded-xl text-xs font-semibold bg-white hover:bg-slate-50 text-slate-700 border border-slate-200/90 shadow-2xs transition-all flex items-center gap-1.5 cursor-pointer"
            >
              <Save className="w-3.5 h-3.5 text-slate-500" />
              <span>{isEditing ? '仅保存修改' : '仅保存到壁纸库'}</span>
            </button>

            <button
              type="button"
              onClick={() => handleSubmit(true)}
              className="px-4 py-2 rounded-xl text-xs font-bold bg-indigo-600 hover:bg-indigo-700 text-white shadow-sm shadow-indigo-600/20 transition-all flex items-center gap-1.5 active:scale-95 cursor-pointer"
            >
              {isEditing ? <Check className="w-3.5 h-3.5" /> : <Play className="w-3.5 h-3.5 fill-white" />}
              <span>{isEditing ? '保存并应用到桌面' : '保存并立即设为壁纸'}</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
