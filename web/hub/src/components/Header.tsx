import React, { useState, useRef, useEffect, useCallback } from 'react';
import { MainTabType } from '../types';
import { LayoutGrid, Sliders, Terminal, RefreshCw, Minus, Square, X, Layers } from 'lucide-react';
import { ipc } from '../services/ipc';

interface HeaderProps {
  activeTab: MainTabType;
  onTabChange: (tab: MainTabType) => void;
  onRefresh: () => void;
  totalWidgets: number;
}

export const Header: React.FC<HeaderProps> = ({
  activeTab,
  onTabChange,
  onRefresh,
  totalWidgets
}) => {
  const [isRefreshing, setIsRefreshing] = useState(false);
  const navRef = useRef<HTMLElement | null>(null);
  const controlsRef = useRef<HTMLDivElement | null>(null);

  const reportInteractiveRects = useCallback(() => {
    const rects: Array<{ x: number; y: number; width: number; height: number }> = [];
    if (navRef.current) {
      const r = navRef.current.getBoundingClientRect();
      rects.push({
        x: Math.floor(r.left) - 4,
        y: 0,
        width: Math.ceil(r.width) + 8,
        height: 44
      });
    }
    if (controlsRef.current) {
      const r = controlsRef.current.getBoundingClientRect();
      rects.push({
        x: Math.floor(r.left) - 4,
        y: 0,
        width: Math.ceil(r.width) + 8,
        height: 44
      });
    }
    if (rects.length > 0) {
      ipc.send('set_titlebar_rects', { rects });
    }
  }, []);

  useEffect(() => {
    reportInteractiveRects();
    const timer = setTimeout(reportInteractiveRects, 150);
    window.addEventListener('resize', reportInteractiveRects);
    let observer: ResizeObserver | null = null;
    if (typeof ResizeObserver !== 'undefined') {
      observer = new ResizeObserver(() => reportInteractiveRects());
      if (navRef.current) observer.observe(navRef.current);
      if (controlsRef.current) observer.observe(controlsRef.current);
    }
    return () => {
      clearTimeout(timer);
      window.removeEventListener('resize', reportInteractiveRects);
      if (observer) observer.disconnect();
    };
  }, [reportInteractiveRects, totalWidgets]);

  const handleRefreshClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    setIsRefreshing(true);
    onRefresh();
    setTimeout(() => setIsRefreshing(false), 600);
  };

  const handleMinimize = (e: React.MouseEvent) => {
    e.stopPropagation();
    ipc.send('window_minimize');
  };

  const handleMaximize = (e: React.MouseEvent) => {
    e.stopPropagation();
    ipc.send('window_maximize');
  };

  const handleClose = (e: React.MouseEvent) => {
    e.stopPropagation();
    ipc.send('window_close');
  };

  return (
    <header className="w-full h-11 bg-[#f1f5f9] border-b border-slate-200/90 flex items-center justify-between select-none z-50 sticky top-0">
      {/* Left: Brand Identity + Left-aligned Tabs (leaves wide middle-to-right space as Native Windows Titlebar Drag Area) */}
      <div className="flex items-center h-full">
        {/* Brand Area (Native HTCAPTION Drag Area) */}
        <div className="flex items-center gap-2 pl-3.5 pr-3 h-full pointer-events-none">
          <div className="w-5 h-5 rounded-md bg-indigo-600 flex items-center justify-center shadow-2xs">
            <Layers className="w-3 h-3 text-white" />
          </div>
          <span className="font-bold text-xs tracking-tight text-slate-800">
            壁纸与组件中心
          </span>
        </div>

        {/* Interactive Navigation Tabs (HTCLIENT) */}
        <nav
          ref={navRef}
          className="flex items-center gap-1 bg-slate-200/75 p-1 rounded-lg border border-slate-300/50"
        >
          <button
            type="button"
            onClick={() => onTabChange('studio')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'studio'
                ? 'bg-white text-indigo-600 shadow-2xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-white/60 font-medium'
            }`}
          >
            <Sliders className="w-3.5 h-3.5" />
            <span>动态壁纸</span>
          </button>

          <button
            type="button"
            onClick={() => onTabChange('widgets')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'widgets'
                ? 'bg-white text-indigo-600 shadow-2xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-white/60 font-medium'
            }`}
          >
            <LayoutGrid className="w-3.5 h-3.5" />
            <span>桌面组件</span>
            {totalWidgets > 0 && (
              <span className="px-1.5 py-0.2 rounded-full text-[10px] bg-indigo-50 text-indigo-600 font-mono font-semibold">
                {totalWidgets}
              </span>
            )}
          </button>

          <button
            type="button"
            onClick={() => onTabChange('desktop')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'desktop'
                ? 'bg-white text-indigo-600 shadow-2xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-white/60 font-medium'
            }`}
          >
            <Terminal className="w-3.5 h-3.5" />
            <span>状态与目录</span>
          </button>
        </nav>
      </div>

      {/* Wide Native Windows HTCAPTION Drag Region in Middle */}
      <div className="flex-1 h-full" />

      {/* Right: Refresh + Native Windows 11 Full-Height Caption Buttons (46x44px) */}
      <div
        ref={controlsRef}
        className="flex items-center h-full"
      >
        <button
          type="button"
          onClick={handleRefreshClick}
          className="h-8 px-2.5 mr-1 text-slate-600 hover:text-indigo-600 hover:bg-slate-200/80 rounded-md transition-colors cursor-pointer flex items-center gap-1 text-[11px] font-medium"
          title="刷新状态"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${isRefreshing ? 'animate-spin text-indigo-600' : ''}`} />
          <span className="hidden sm:inline">刷新</span>
        </button>

        <div className="w-px h-4 bg-slate-300/80 mr-0.5" />

        {/* Standard Windows 11 46x44 Caption Buttons */}
        <button
          type="button"
          onClick={handleMinimize}
          className="w-[46px] h-11 flex items-center justify-center text-slate-700 hover:bg-slate-200/90 active:bg-slate-300/90 transition-colors cursor-pointer"
          title="最小化"
        >
          <Minus className="w-4 h-4 stroke-[1.75]" />
        </button>

        <button
          type="button"
          onClick={handleMaximize}
          className="w-[46px] h-11 flex items-center justify-center text-slate-700 hover:bg-slate-200/90 active:bg-slate-300/90 transition-colors cursor-pointer"
          title="最大化 / 还原"
        >
          <Square className="w-3.5 h-3.5 stroke-[1.75]" />
        </button>

        <button
          type="button"
          onClick={handleClose}
          className="w-[46px] h-11 flex items-center justify-center text-slate-700 hover:bg-[#e81123] hover:text-white active:bg-[#c50f1f] transition-colors cursor-pointer"
          title="关闭"
        >
          <X className="w-4 h-4 stroke-[1.75]" />
        </button>
      </div>
    </header>
  );
};
