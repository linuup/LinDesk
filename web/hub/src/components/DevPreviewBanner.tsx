import React, { useState } from 'react';
import { ipc } from '../services/ipc';
import { Wrench, ChevronDown, ChevronUp, Sparkles, Plus, Layers, Film } from 'lucide-react';

export const DevPreviewBanner: React.FC = () => {
  // Only display in browser preview mode (when not embedded inside LinDesk CEF host)
  if (ipc.getIsCEF()) {
    return null;
  }

  const [collapsed, setCollapsed] = useState(false);
  const [currentMode, setCurrentMode] = useState<'rich' | 'empty' | 'wallpapers_only' | 'widgets_only'>(() => ipc.getMockMode());

  const handleSwitchMode = (mode: 'rich' | 'empty' | 'wallpapers_only' | 'widgets_only') => {
    setCurrentMode(mode);
    ipc.setMockMode(mode);
  };

  const handleSimulateDrag = () => {
    ipc.simulateDragWallpaper('示例动态壁纸 · 晨光微风', 'https://assets.mixkit.co/videos/preview/mixkit-tree-branches-in-the-breeze-1188-large.mp4');
  };

  if (collapsed) {
    return (
      <div className="fixed bottom-3 right-3 z-50">
        <button
          type="button"
          onClick={() => setCollapsed(false)}
          className="flex items-center gap-1.5 px-3 py-1.5 rounded-full bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold shadow-lg shadow-indigo-500/25 transition-all hover:scale-105 active:scale-95 cursor-pointer select-none"
          title="展开实时开发与预览调试栏"
        >
          <Wrench className="w-3.5 h-3.5" />
          <span>开发调试条</span>
          <ChevronUp className="w-3.5 h-3.5" />
        </button>
      </div>
    );
  }

  return (
    <div className="fixed bottom-3 right-3 z-50 max-w-xl animate-in slide-in-from-bottom-2 duration-150 select-none">
      <div className="bg-white/95 dark:bg-[#131927]/95 backdrop-blur-md rounded-2xl border border-indigo-200/90 dark:border-indigo-800/80 p-3 shadow-2xl flex flex-col gap-2.5 text-xs text-slate-700 dark:text-slate-200">
        {/* Header */}
        <div className="flex items-center justify-between gap-3 pb-2 border-b border-slate-200/80 dark:border-slate-800">
          <div className="flex items-center gap-2">
            <span className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse" />
            <span className="font-bold text-slate-900 dark:text-slate-100 flex items-center gap-1.5">
              <Sparkles className="w-3.5 h-3.5 text-indigo-500" />
              浏览器实时开发与预览模式 (Vite HMR)
            </span>
          </div>

          <button
            type="button"
            onClick={() => setCollapsed(true)}
            className="p-1 rounded-md text-slate-400 hover:text-slate-600 dark:hover:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800 transition cursor-pointer"
            title="收起调试条"
          >
            <ChevronDown className="w-4 h-4" />
          </button>
        </div>

        {/* State Switchers */}
        <div className="flex flex-wrap items-center gap-1.5">
          <span className="text-[11px] text-slate-500 dark:text-slate-400">切换预览数据:</span>

          <button
            type="button"
            onClick={() => handleSwitchMode('rich')}
            className={`px-2.5 py-1 rounded-lg text-[11px] font-semibold transition cursor-pointer ${
              currentMode === 'rich'
                ? 'bg-indigo-600 text-white shadow-xs'
                : 'bg-slate-100 dark:bg-slate-800 hover:bg-slate-200 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-300'
            }`}
          >
            丰富数据 (7组件3壁纸)
          </button>

          <button
            type="button"
            onClick={() => handleSwitchMode('empty')}
            className={`px-2.5 py-1 rounded-lg text-[11px] font-semibold transition cursor-pointer ${
              currentMode === 'empty'
                ? 'bg-amber-600 text-white shadow-xs'
                : 'bg-slate-100 dark:bg-slate-800 hover:bg-slate-200 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-300'
            }`}
            title="预览零资源新手起步引导页"
          >
            零资源空态 (0组件0壁纸)
          </button>

          <button
            type="button"
            onClick={() => handleSwitchMode('wallpapers_only')}
            className={`px-2.5 py-1 rounded-lg text-[11px] font-semibold transition cursor-pointer ${
              currentMode === 'wallpapers_only'
                ? 'bg-indigo-600 text-white shadow-xs'
                : 'bg-slate-100 dark:bg-slate-800 hover:bg-slate-200 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-300'
            }`}
          >
            <Film className="w-3 h-3 inline mr-1" />
            仅壁纸 (3壁纸0组件)
          </button>

          <button
            type="button"
            onClick={() => handleSwitchMode('widgets_only')}
            className={`px-2.5 py-1 rounded-lg text-[11px] font-semibold transition cursor-pointer ${
              currentMode === 'widgets_only'
                ? 'bg-indigo-600 text-white shadow-xs'
                : 'bg-slate-100 dark:bg-slate-800 hover:bg-slate-200 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-300'
            }`}
          >
            <Layers className="w-3 h-3 inline mr-1" />
            仅组件 (7组件0壁纸)
          </button>
        </div>

        {/* Action Triggers */}
        <div className="flex items-center gap-2 pt-1 border-t border-slate-100 dark:border-slate-800/80">
          <span className="text-[11px] text-slate-500 dark:text-slate-400">模拟交互:</span>
          <button
            type="button"
            onClick={handleSimulateDrag}
            className="px-2.5 py-0.5 rounded-md bg-indigo-50 dark:bg-indigo-950/60 hover:bg-indigo-100 dark:hover:bg-indigo-900/60 text-indigo-700 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-800/50 text-[11px] font-medium flex items-center gap-1 transition cursor-pointer"
          >
            <Plus className="w-3 h-3" />
            <span>模拟拖入壁纸文件</span>
          </button>
        </div>
      </div>
    </div>
  );
};
