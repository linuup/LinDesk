import React, { useEffect } from 'react';
import { WallpaperInfo } from '../types';
import { Trash2, AlertTriangle, X, Film, Image as ImageIcon, Sparkles } from 'lucide-react';

interface WallpaperDeleteModalProps {
  wallpaper: WallpaperInfo | null;
  isActive: boolean;
  onClose: () => void;
  onConfirm: (id: string, packagePath: string, name: string) => void;
}

export const WallpaperDeleteModal: React.FC<WallpaperDeleteModalProps> = ({
  wallpaper,
  isActive,
  onClose,
  onConfirm
}) => {
  // ESC key to close
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        onClose();
      }
    };
    if (wallpaper) {
      window.addEventListener('keydown', handleKeyDown);
    }
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [wallpaper, onClose]);

  if (!wallpaper) return null;

  const isVideo = wallpaper.wallpaperType === 'video';
  const isWeb = wallpaper.wallpaperType === 'web';
  const preview = wallpaper.previewUrl || wallpaper.mediaUrl;

  const handleConfirm = () => {
    onConfirm(wallpaper.id, wallpaper.packagePath, wallpaper.name);
    onClose();
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 dark:bg-black/75 backdrop-blur-sm animate-in fade-in duration-150 select-none"
      onClick={onClose}
    >
      <div
        className="bg-white dark:bg-[#131927] rounded-2xl border border-slate-200 dark:border-slate-800/90 shadow-2xl w-full max-w-md overflow-hidden animate-in zoom-in-95 duration-150 flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="px-5 py-4 border-b border-slate-100 dark:border-slate-800/80 flex items-center justify-between bg-slate-50/70 dark:bg-[#0f1422]/60">
          <div className="flex items-center gap-3">
            <div className="w-9 h-9 rounded-xl bg-rose-50 dark:bg-rose-950/50 text-rose-600 dark:text-rose-400 border border-rose-200/80 dark:border-rose-900/50 flex items-center justify-center shadow-xs">
              <Trash2 className="w-4 h-4" />
            </div>
            <div>
              <h3 className="text-sm font-bold text-slate-900 dark:text-slate-100">
                确认移除壁纸？
              </h3>
              <p className="text-[11px] text-slate-500 dark:text-slate-400">
                此操作将从壁纸工坊画廊中移除该项记录
              </p>
            </div>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="w-7 h-7 rounded-lg text-slate-400 hover:text-slate-700 dark:hover:text-white hover:bg-slate-200/60 dark:hover:bg-slate-800 flex items-center justify-center transition-colors cursor-pointer"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Content Body */}
        <div className="p-5 flex flex-col gap-4">
          {/* Target Wallpaper Preview Card */}
          <div className="p-3 rounded-xl bg-slate-50 dark:bg-[#0f1422] border border-slate-200/80 dark:border-slate-800 flex gap-3.5 items-center">
            {/* Thumbnail */}
            <div className="w-24 aspect-video rounded-lg overflow-hidden bg-slate-900 shrink-0 relative border border-slate-200/60 dark:border-slate-800/80 shadow-xs">
              {preview ? (
                <img
                  src={preview}
                  alt={wallpaper.name}
                  className="w-full h-full object-cover"
                />
              ) : (
                <div className="w-full h-full flex items-center justify-center text-slate-600">
                  {isVideo ? <Film className="w-5 h-5" /> : <ImageIcon className="w-5 h-5" />}
                </div>
              )}
              <div className="absolute bottom-1 right-1 px-1.5 py-0.5 rounded text-[9px] font-bold bg-black/75 text-white backdrop-blur-xs flex items-center gap-1">
                {isVideo ? <Film className="w-2.5 h-2.5" /> : isWeb ? <Sparkles className="w-2.5 h-2.5" /> : <ImageIcon className="w-2.5 h-2.5" />}
                <span>{isVideo ? '视频' : isWeb ? '交互' : '图片'}</span>
              </div>
            </div>

            {/* Meta */}
            <div className="flex-1 min-w-0">
              <h4 className="text-xs font-bold text-slate-900 dark:text-slate-100 truncate mb-1">
                {wallpaper.name}
              </h4>
              <p className="text-[11px] text-slate-500 dark:text-slate-400 truncate mb-1.5">
                {wallpaper.description || '自定义壁纸'}
              </p>
              {wallpaper.author && (
                <span className="text-[10px] text-slate-400 dark:text-slate-500 font-medium">
                  来源：{wallpaper.author}
                </span>
              )}
            </div>
          </div>

          {/* Active Wallpaper Warning Alert */}
          {isActive ? (
            <div className="p-3 rounded-xl bg-amber-50 dark:bg-amber-950/40 border border-amber-200/90 dark:border-amber-900/50 flex items-start gap-2.5 animate-in fade-in">
              <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400 shrink-0 mt-0.5" />
              <div className="flex-1 text-[11px] leading-relaxed text-amber-800 dark:text-amber-200 font-medium">
                <span className="font-bold">注意：该壁纸当前正作为桌面背景运行！</span>
                <p className="text-amber-700/90 dark:text-amber-300/80 mt-0.5">
                  确认移除后，桌面壁纸将自动暂停并恢复为系统默认背景。
                </p>
              </div>
            </div>
          ) : (
            <p className="text-[11px] text-slate-500 dark:text-slate-400 leading-relaxed px-1">
              若该壁纸为已归档副本，其本地数据将被安全移除。您之后仍可随时通过文件拖拽或导入重新添加。
            </p>
          )}
        </div>

        {/* Footer Buttons */}
        <div className="px-5 py-3.5 border-t border-slate-100 dark:border-slate-800/80 bg-slate-50/70 dark:bg-[#0f1422]/60 flex items-center justify-end gap-2.5">
          <button
            type="button"
            onClick={onClose}
            className="px-4 py-2 rounded-xl text-xs font-semibold text-slate-600 dark:text-slate-300 hover:text-slate-900 dark:hover:text-white hover:bg-slate-200/60 dark:hover:bg-slate-800 transition-colors cursor-pointer"
          >
            取消
          </button>
          <button
            type="button"
            onClick={handleConfirm}
            className="px-4 py-2 rounded-xl text-xs font-bold bg-rose-600 hover:bg-rose-500 text-white shadow-sm shadow-rose-600/30 transition-all flex items-center gap-1.5 active:scale-95 cursor-pointer"
          >
            <Trash2 className="w-3.5 h-3.5" />
            <span>确认移除</span>
          </button>
        </div>
      </div>
    </div>
  );
};

export default WallpaperDeleteModal;
