import React, { useRef, useEffect, useCallback } from 'react';
import { MainTabType } from '../types';
import { LayoutGrid, Sliders, Terminal, Minus, Square, X, Compass, Sun, Moon } from 'lucide-react';
import { ipc } from '../services/ipc';
import { HubIcon } from './HubIcon';

interface HeaderProps {
  activeTab: MainTabType;
  onTabChange: (tab: MainTabType) => void;
  totalWidgets: number;
  theme: 'light' | 'dark';
  onToggleTheme: () => void;
}

export const Header: React.FC<HeaderProps> = ({
  activeTab,
  onTabChange,
  totalWidgets,
  theme,
  onToggleTheme
}) => {
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
    <header className="w-full h-11 bg-white/95 dark:bg-[#0f1422] border-b border-slate-200/90 dark:border-slate-800/90 flex items-center justify-between select-none z-50 sticky top-0 backdrop-blur-md transition-colors">
      {/* Left: Brand Identity + Left-aligned Tabs */}
      <div className="flex items-center h-full">
        {/* Brand Area (Native HTCAPTION Drag Area) */}
        <div className="flex items-center gap-2 pl-3.5 pr-3 h-full pointer-events-none">
          <HubIcon className="w-5 h-5 drop-shadow-xs" size={20} />
          <span className="font-bold text-xs tracking-tight text-slate-900 dark:text-slate-100">
            LinDesk Hub
          </span>
        </div>

        {/* Interactive Navigation Tabs (HTCLIENT) */}
        <nav
          ref={navRef}
          className="flex items-center gap-1 bg-slate-100/90 dark:bg-slate-900/90 p-1 rounded-lg border border-slate-200/80 dark:border-slate-800/80"
        >
          <button
            type="button"
            onClick={() => onTabChange('overview')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'overview'
                ? 'bg-indigo-600 text-white shadow-xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-slate-200/60 dark:text-slate-400 dark:hover:text-slate-100 dark:hover:bg-slate-800/70 font-medium'
            }`}
          >
            <Compass className="w-3.5 h-3.5" />
            <span>灵感探索</span>
          </button>

          <button
            type="button"
            onClick={() => onTabChange('studio')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'studio'
                ? 'bg-indigo-600 text-white shadow-xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-slate-200/60 dark:text-slate-400 dark:hover:text-slate-100 dark:hover:bg-slate-800/70 font-medium'
            }`}
          >
            <Sliders className="w-3.5 h-3.5" />
            <span>壁纸工坊</span>
          </button>

          <button
            type="button"
            onClick={() => onTabChange('widgets')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'widgets'
                ? 'bg-indigo-600 text-white shadow-xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-slate-200/60 dark:text-slate-400 dark:hover:text-slate-100 dark:hover:bg-slate-800/70 font-medium'
            }`}
          >
            <LayoutGrid className="w-3.5 h-3.5" />
            <span>桌面组件</span>
            {totalWidgets > 0 && (
              <span className="px-1.5 py-0.2 rounded-full text-[10px] bg-indigo-100 text-indigo-700 dark:bg-indigo-950/80 dark:text-indigo-300 border border-indigo-200 dark:border-indigo-800/50 font-mono font-semibold">
                {totalWidgets}
              </span>
            )}
          </button>

          <button
            type="button"
            onClick={() => onTabChange('desktop')}
            className={`px-3 py-1 rounded-md text-xs transition-all duration-150 flex items-center gap-1.5 cursor-pointer ${
              activeTab === 'desktop'
                ? 'bg-indigo-600 text-white shadow-xs font-semibold'
                : 'text-slate-600 hover:text-slate-900 hover:bg-slate-200/60 dark:text-slate-400 dark:hover:text-slate-100 dark:hover:bg-slate-800/70 font-medium'
            }`}
          >
            <Terminal className="w-3.5 h-3.5" />
            <span>控制台</span>
          </button>
        </nav>
      </div>

      {/* Wide Native Windows HTCAPTION Drag Region in Middle */}
      <div className="flex-1 h-full" />

      {/* Right: Theme Switcher + Native Windows 11 Caption Buttons */}
      <div
        ref={controlsRef}
        className="flex items-center h-full"
      >
        {/* Theme Toggle Button (☀️ / 🌙) */}
        <button
          type="button"
          onClick={onToggleTheme}
          className="w-10 h-11 flex items-center justify-center text-slate-500 hover:text-slate-900 hover:bg-slate-100 dark:text-slate-400 dark:hover:text-white dark:hover:bg-slate-800/90 active:bg-slate-200 dark:active:bg-slate-700/90 transition-colors cursor-pointer"
          title={theme === 'dark' ? '切换为亮色模式' : '切换为深色模式'}
        >
          {theme === 'dark' ? (
            <Sun className="w-4 h-4 text-amber-400 transition-transform duration-200 hover:rotate-45" />
          ) : (
            <Moon className="w-4 h-4 text-slate-600 transition-transform duration-200 hover:-rotate-12" />
          )}
        </button>

        {/* Standard Windows 11 46x44 Caption Buttons */}
        <button
          type="button"
          onClick={handleMinimize}
          className="w-[46px] h-11 flex items-center justify-center text-slate-500 hover:text-slate-900 hover:bg-slate-100 dark:text-slate-400 dark:hover:text-white dark:hover:bg-slate-800/90 active:bg-slate-200 dark:active:bg-slate-700/90 transition-colors cursor-pointer"
          title="最小化"
        >
          <Minus className="w-4 h-4 stroke-[1.75]" />
        </button>

        <button
          type="button"
          onClick={handleMaximize}
          className="w-[46px] h-11 flex items-center justify-center text-slate-500 hover:text-slate-900 hover:bg-slate-100 dark:text-slate-400 dark:hover:text-white dark:hover:bg-slate-800/90 active:bg-slate-200 dark:active:bg-slate-700/90 transition-colors cursor-pointer"
          title="最大化 / 还原"
        >
          <Square className="w-3.5 h-3.5 stroke-[1.75]" />
        </button>

        <button
          type="button"
          onClick={handleClose}
          className="w-[46px] h-11 flex items-center justify-center text-slate-500 hover:bg-[#e81123] hover:text-white dark:text-slate-400 dark:hover:bg-[#e81123] dark:hover:text-white active:bg-[#c50f1f] transition-colors cursor-pointer"
          title="关闭"
        >
          <X className="w-4 h-4 stroke-[1.75]" />
        </button>
      </div>
    </header>
  );
};
