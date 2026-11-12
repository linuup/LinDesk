import React, { useState } from 'react';
import { WidgetInfo, WidgetSettingField } from '../types';
import { 
  Play, 
  Square, 
  FolderOpen, 
  Trash2, 
  Plus, 
  Code, 
  Search, 
  Clock, 
  CheckSquare, 
  StickyNote, 
  Activity, 
  Timer, 
  Droplets, 
  CloudSun, 
  Layers, 
  SlidersHorizontal, 
  Save, 
  RotateCcw,
  Sparkles,
  Move,
  Monitor,
  Zap
} from 'lucide-react';

interface WidgetCenterProps {
  widgets: WidgetInfo[];
  runtimeEnabled: boolean;
  showBounds?: boolean;
  onToggleMaster: (enabled: boolean) => void;
  onToggleBounds?: (enabled: boolean) => void;
  onLaunch: (id: string, packagePath: string) => void;
  onStop: (id: string) => void;
  onRestart?: (id: string) => void;
  onTogglePermission?: (id: string, perm: string, granted: boolean) => void;
  onClearStorage?: (id: string, name: string) => void;
  onOpenFolder: (id: string, path: string) => void;
  onOpenWidgetsDir: () => void;
  onShowDevTools?: (id: string) => void;
  onUninstall: (id: string, packagePath?: string, name?: string) => void;
  onSaveSettings: (widgetId: string, settings: Record<string, any>) => void;
  onOpenCreateModal: () => void;
  onImportDevProject: () => void;
}

export const WidgetCenter: React.FC<WidgetCenterProps> = ({
  widgets,
  runtimeEnabled,
  onToggleMaster,
  onLaunch,
  onStop,
  onOpenFolder,
  onOpenWidgetsDir,
  onUninstall,
  onSaveSettings,
  onOpenCreateModal,
  onImportDevProject,
}) => {
  const [filterMode, setFilterMode] = useState<'all' | 'running' | 'official' | 'dev'>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedSettingsId, setExpandedSettingsId] = useState<string | null>(null);
  const [formValuesMap, setFormValuesMap] = useState<Record<string, Record<string, any>>>({});
  const [saveSuccessId, setSaveSuccessId] = useState<string | null>(null);

  const totalCount = widgets.length;
  const runningCount = widgets.filter(w => w.running).length;

  const filteredWidgets = widgets.filter(w => {
    if (searchQuery.trim()) {
      const q = searchQuery.toLowerCase();
      const match = (w.name || '').toLowerCase().includes(q) || 
                    (w.id || '').toLowerCase().includes(q) || 
                    (w.description || '').toLowerCase().includes(q);
      if (!match) return false;
    }
    if (filterMode === 'running') return w.running;
    if (filterMode === 'official') return !w.isDevelopment;
    if (filterMode === 'dev') return w.isDevelopment;
    return true;
  });

  const getWidgetVisual = (id: string, isDev: boolean) => {
    const norm = id.toLowerCase();
    if (norm.includes('clock')) {
      return { 
        icon: Clock, 
        color: 'text-indigo-600 dark:text-indigo-400', 
        bg: 'bg-indigo-50 border-indigo-200/80 dark:bg-indigo-950/60 dark:border-indigo-800/80' 
      };
    }
    if (norm.includes('todo')) {
      return { 
        icon: CheckSquare, 
        color: 'text-emerald-600 dark:text-emerald-400', 
        bg: 'bg-emerald-50 border-emerald-200/80 dark:bg-emerald-950/60 dark:border-emerald-800/80' 
      };
    }
    if (norm.includes('notes')) {
      return { 
        icon: StickyNote, 
        color: 'text-amber-600 dark:text-amber-400', 
        bg: 'bg-amber-50 border-amber-200/80 dark:bg-amber-950/60 dark:border-amber-800/80' 
      };
    }
    if (norm.includes('sysmonitor') || norm.includes('system')) {
      return { 
        icon: Activity, 
        color: 'text-blue-600 dark:text-blue-400', 
        bg: 'bg-blue-50 border-blue-200/80 dark:bg-blue-950/60 dark:border-blue-800/80' 
      };
    }
    if (norm.includes('timer')) {
      return { 
        icon: Timer, 
        color: 'text-purple-600 dark:text-purple-400', 
        bg: 'bg-purple-50 border-purple-200/80 dark:bg-purple-950/60 dark:border-purple-800/80' 
      };
    }
    if (norm.includes('water')) {
      return { 
        icon: Droplets, 
        color: 'text-cyan-600 dark:text-cyan-400', 
        bg: 'bg-cyan-50 border-cyan-200/80 dark:bg-cyan-950/60 dark:border-cyan-800/80' 
      };
    }
    if (norm.includes('weather')) {
      return { 
        icon: CloudSun, 
        color: 'text-sky-600 dark:text-sky-400', 
        bg: 'bg-sky-50 border-sky-200/80 dark:bg-sky-950/60 dark:border-sky-800/80' 
      };
    }
    return {
      icon: isDev ? Code : Layers,
      color: isDev ? 'text-amber-600 dark:text-amber-400' : 'text-slate-600 dark:text-slate-400',
      bg: isDev ? 'bg-amber-50 border-amber-200/80 dark:bg-amber-950/60 dark:border-amber-800/80' : 'bg-slate-100 border-slate-200 dark:bg-slate-900 dark:border-slate-800',
    };
  };

  const WidgetIconView: React.FC<{ widget: WidgetInfo }> = ({ widget }) => {
    const [imgError, setImgError] = useState(false);
    const visual = getWidgetVisual(widget.id, widget.isDevelopment);
    const IconComp = visual.icon;

    const iconUrl = React.useMemo(() => {
      if (widget.packagePath) {
        const clean = widget.packagePath.replace(/\\/g, '/');
        const prefix = clean.startsWith('/') ? 'file://' : 'file:///';
        return `${prefix}${clean}/icon.png`;
      }
      return null;
    }, [widget.packagePath]);

    if (iconUrl && !imgError) {
      return (
        <div className="w-10 h-10 rounded-xl overflow-hidden bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shadow-2xs flex items-center justify-center p-1.5 shrink-0">
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
      <div className={`w-10 h-10 rounded-xl ${visual.bg} flex items-center justify-center shrink-0 border shadow-2xs transition-colors`}>
        <IconComp className={`w-5 h-5 ${visual.color}`} />
      </div>
    );
  };

  const handleToggleSettings = (w: WidgetInfo) => {
    if (expandedSettingsId === w.id) {
      setExpandedSettingsId(null);
      return;
    }
    setExpandedSettingsId(w.id);
    if (!formValuesMap[w.id]) {
      const initial: Record<string, any> = { ...(w.settingsValues || {}) };
      if (w.settingsSchema) {
        w.settingsSchema.forEach(field => {
          if (initial[field.key] === undefined && field.default !== undefined) {
            initial[field.key] = field.default;
          }
        });
      }
      setFormValuesMap(prev => ({ ...prev, [w.id]: initial }));
    }
  };

  const handleFieldChange = (widgetId: string, key: string, val: any) => {
    setFormValuesMap(prev => ({
      ...prev,
      [widgetId]: { ...(prev[widgetId] || {}), [key]: val },
    }));
  };

  const handleResetDefaults = (w: WidgetInfo) => {
    const defaults: Record<string, any> = {};
    if (w.settingsSchema) {
      w.settingsSchema.forEach(field => {
        if (field.default !== undefined) defaults[field.key] = field.default;
      });
    }
    setFormValuesMap(prev => ({ ...prev, [w.id]: defaults }));
  };

  const handleSaveSettingsClick = (w: WidgetInfo) => {
    const values = formValuesMap[w.id] || w.settingsValues || {};
    onSaveSettings(w.id, values);
    setSaveSuccessId(w.id);
    setTimeout(() => {
      setSaveSuccessId(prev => (prev === w.id ? null : prev));
    }, 2000);
  };

  return (
    <div className="space-y-4 animate-in fade-in duration-150 pb-8">
      {/* 1. Clean Top Toolbar (Master Switch + Actions + Filter) */}
      <div className="bg-white dark:bg-[#131927]/90 rounded-2xl p-4 border border-slate-200/90 dark:border-slate-800/90 shadow-xs dark:shadow-md space-y-3.5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          {/* Master Toggle */}
          <div className="flex items-center gap-3">
            <div className="flex items-center gap-2.5 bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 px-3.5 py-1.5 rounded-xl shadow-xs">
              <span className="text-xs font-semibold text-slate-700 dark:text-slate-300">桌面小组件总开关:</span>
              <button
                type="button"
                onClick={() => onToggleMaster(!runtimeEnabled)}
                className={`relative inline-flex h-5 w-9 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ease-in-out focus:outline-none ${
                  runtimeEnabled ? 'bg-indigo-600' : 'bg-slate-300 dark:bg-slate-700'
                }`}
                title={runtimeEnabled ? '点击关闭所有已加载的桌面小组件（不影响动态壁纸）' : '点击开启所有已加载的桌面小组件'}
              >
                <span
                  className={`pointer-events-none inline-block h-4 w-4 transform rounded-full bg-white shadow-xs ring-0 transition duration-200 ease-in-out ${
                    runtimeEnabled ? 'translate-x-4' : 'translate-x-0'
                  }`}
                />
              </button>
              <span className={`text-[11px] font-bold ${runtimeEnabled ? 'text-indigo-600 dark:text-indigo-400' : 'text-slate-400 dark:text-slate-500'}`}>
                {runtimeEnabled ? '已开启' : '已关闭'}
              </span>
              <span className="text-[11px] text-slate-400 dark:text-slate-500 hidden sm:inline">
                (仅控制小组件，不含壁纸)
              </span>
            </div>
          </div>

          {/* Quick Actions */}
          <div className="flex items-center gap-2">
            <button
              onClick={onOpenCreateModal}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white flex items-center gap-1.5 shadow-xs transition-all active:scale-95 cursor-pointer"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>新建组件</span>
            </button>
            <button
              onClick={onImportDevProject}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-white dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 text-slate-700 dark:text-slate-200 border border-slate-200 dark:border-slate-800 flex items-center gap-1.5 shadow-2xs transition-all active:scale-95 cursor-pointer"
            >
              <Code className="w-3.5 h-3.5 text-amber-500 dark:text-amber-400" />
              <span>导入本地组件</span>
            </button>
            <button
              onClick={onOpenWidgetsDir}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-white dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 text-slate-700 dark:text-slate-200 border border-slate-200 dark:border-slate-800 flex items-center gap-1.5 shadow-2xs transition-all active:scale-95 cursor-pointer"
            >
              <FolderOpen className="w-3.5 h-3.5 text-slate-500 dark:text-slate-400" />
              <span>打开文件夹</span>
            </button>
          </div>
        </div>

        {/* Filter Pills & Search */}
        <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-3 pt-3 border-t border-slate-200/80 dark:border-slate-800/80">
          <div className="flex items-center gap-1.5">
            <button
              onClick={() => setFilterMode('all')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all cursor-pointer ${
                filterMode === 'all'
                  ? 'bg-indigo-600 text-white font-semibold shadow-xs'
                  : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
              }`}
            >
              全部 ({totalCount})
            </button>
            <button
              onClick={() => setFilterMode('running')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all cursor-pointer ${
                filterMode === 'running'
                  ? 'bg-emerald-600 text-white font-semibold shadow-xs'
                  : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
              }`}
            >
              运行中 ({runningCount})
            </button>
            <button
              onClick={() => setFilterMode('official')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all cursor-pointer ${
                filterMode === 'official'
                  ? 'bg-slate-800 text-white font-semibold shadow-xs'
                  : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
              }`}
            >
              官方内置
            </button>
            <button
              onClick={() => setFilterMode('dev')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all cursor-pointer ${
                filterMode === 'dev'
                  ? 'bg-amber-600 text-white font-semibold shadow-xs'
                  : 'bg-white dark:bg-slate-900 text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 border border-slate-200 dark:border-slate-800/80 shadow-2xs'
              }`}
            >
              自定义
            </button>
          </div>

          <div className="relative">
            <Search className="w-3.5 h-3.5 text-slate-400 dark:text-slate-500 absolute left-3 top-1/2 -translate-y-1/2" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="搜索组件..."
              className="pl-8 pr-3 py-1.5 rounded-xl text-xs bg-white dark:bg-slate-900 border border-slate-200 dark:border-slate-800 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-2 focus:ring-indigo-500/20 focus:border-indigo-500 transition-all placeholder:text-slate-400 dark:placeholder:text-slate-500 w-full sm:w-56 shadow-2xs"
            />
          </div>
        </div>
      </div>

      {/* 2. Compact Modern Widget Cards Grid */}
      {totalCount === 0 ? (
        <div className="space-y-6 pt-1 animate-fade-in select-none">
          {/* 顶部欢迎横幅 (Hero Welcome Banner) */}
          <div className="relative rounded-3xl overflow-hidden bg-gradient-to-br from-indigo-900 via-slate-900 to-slate-950 text-white p-8 sm:p-11 border border-indigo-500/20 shadow-xl flex flex-col items-center justify-center text-center">
            {/* 居中环境光晕特效 */}
            <div className="absolute left-1/2 -top-24 -translate-x-1/2 w-96 h-96 bg-indigo-500/20 rounded-full blur-3xl pointer-events-none" />
            <div className="absolute left-1/2 -bottom-24 -translate-x-1/2 w-80 h-80 bg-violet-500/15 rounded-full blur-3xl pointer-events-none" />

            <div className="relative z-10 max-w-xl mx-auto flex flex-col items-center text-center">
              <div className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full bg-white/10 backdrop-blur-md border border-white/15 text-indigo-200 text-xs font-medium mb-3.5 shadow-xs">
                <Sparkles className="w-3.5 h-3.5 text-indigo-300 animate-pulse" />
                <span>桌面随手小工具 · 效率与美观兼具</span>
              </div>

              <h2 className="text-2xl sm:text-3xl font-extrabold text-white tracking-tight leading-snug">
                开启你的第一个桌面小工具
              </h2>

              <div className="mt-6 flex flex-wrap items-center justify-center gap-3.5">
                <button
                  type="button"
                  onClick={onOpenCreateModal}
                  className="px-5 py-2.5 rounded-xl bg-indigo-500 hover:bg-indigo-400 text-white text-xs sm:text-sm font-semibold flex items-center gap-2 shadow-lg shadow-indigo-500/30 transition-all hover:scale-[1.02] active:scale-95 cursor-pointer"
                >
                  <Plus className="w-4 h-4" />
                  <span>新建小工具</span>
                </button>

                <button
                  type="button"
                  onClick={onImportDevProject}
                  className="px-4 py-2.5 rounded-xl bg-white/10 hover:bg-white/20 text-white text-xs sm:text-sm font-semibold border border-white/20 backdrop-blur-md flex items-center gap-2 transition-all hover:scale-[1.02] active:scale-95 cursor-pointer"
                >
                  <Code className="w-4 h-4 text-amber-400" />
                  <span>导入本地组件</span>
                </button>
              </div>
            </div>
          </div>

          {/* 使用技巧小指南 (Feature Tips) */}
          <div className="bg-white/95 dark:bg-[#131927]/90 rounded-2xl p-6 border border-slate-200/90 dark:border-slate-800/90 shadow-xs space-y-4">
            <div className="flex items-center gap-2">
              <Sparkles className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />
              <h4 className="text-xs font-bold text-slate-900 dark:text-slate-100 uppercase tracking-wider">
                桌面小工具玩法技巧
              </h4>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-4 gap-3">
              <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/70 dark:border-slate-800/60 flex items-start gap-2.5">
                <Move className="w-4 h-4 text-indigo-500 mt-0.5 shrink-0" />
                <div>
                  <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200">鼠标随心拖动</h5>
                  <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400">
                    在桌面上按住小工具即可自由拖到任意角落。
                  </p>
                </div>
              </div>

              <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/70 dark:border-slate-800/60 flex items-start gap-2.5">
                <SlidersHorizontal className="w-4 h-4 text-emerald-500 mt-0.5 shrink-0" />
                <div>
                  <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200">边角自由缩放</h5>
                  <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400">
                    拖动小工具的边角，放大或缩小尺寸随心调节。
                  </p>
                </div>
              </div>

              <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/70 dark:border-slate-800/60 flex items-start gap-2.5">
                <Zap className="w-4 h-4 text-amber-500 mt-0.5 shrink-0" />
                <div>
                  <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200">随用随关</h5>
                  <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400">
                    随时在右上角一键开启或关闭，清爽不留痕迹。
                  </p>
                </div>
              </div>

              <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900/60 border border-slate-200/70 dark:border-slate-800/60 flex items-start gap-2.5">
                <Monitor className="w-4 h-4 text-sky-500 mt-0.5 shrink-0" />
                <div>
                  <h5 className="text-xs font-bold text-slate-900 dark:text-slate-200">不遮挡文件</h5>
                  <p className="mt-0.5 text-[11px] text-slate-500 dark:text-slate-400">
                    与桌面完全融为一体，原本的桌面图标正常点击。
                  </p>
                </div>
              </div>
            </div>
          </div>
        </div>
      ) : filteredWidgets.length === 0 ? (
        <div className="py-16 text-center bg-white dark:bg-[#131927]/90 rounded-2xl border border-slate-200/90 dark:border-slate-800/90 p-6 shadow-xs">
          <Search className="w-8 h-8 text-slate-400 dark:text-slate-500 mx-auto mb-2" />
          <p className="text-xs font-semibold text-slate-700 dark:text-slate-300">未找到符合条件的小组件</p>
          <p className="text-[11px] text-slate-400 dark:text-slate-500 mt-1">尝试更换关键词，或切换筛选标签</p>
          <button
            type="button"
            onClick={() => { setFilterMode('all'); setSearchQuery(''); }}
            className="mt-3 text-xs text-indigo-600 dark:text-indigo-400 hover:underline font-medium cursor-pointer"
          >
            清除筛选条件
          </button>
        </div>
      ) : (
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {filteredWidgets.map((w) => {
          const isExpanded = expandedSettingsId === w.id;
          const hasSettings = w.hasSettings || (w.settingsSchema && w.settingsSchema.length > 0);

          return (
            <div
              key={w.id}
              className={`rounded-2xl border transition-all duration-150 bg-white dark:bg-[#131927]/90 shadow-xs hover:shadow-md dark:hover:border-slate-700 flex flex-col justify-between overflow-hidden ${
                w.running ? 'border-indigo-500/60 ring-1 ring-indigo-500/20' : 'border-slate-200/90 dark:border-slate-800/90 hover:border-slate-300'
              }`}
            >
              {/* Card Header & Body */}
              <div className="p-4 space-y-3">
                <div className="flex items-start justify-between gap-3">
                  <div className="flex items-center gap-3 min-w-0">
                    <WidgetIconView widget={w} />
                    <div className="min-w-0">
                      <div className="flex items-center gap-2">
                        <h4 className="text-xs font-bold text-slate-900 dark:text-slate-100 truncate">{w.name}</h4>
                        <span className="text-[10px] text-slate-400 dark:text-slate-500 font-mono">{w.version || '1.0'}</span>
                      </div>
                      <p className="text-[11px] text-slate-500 dark:text-slate-400 line-clamp-1 mt-0.5">{w.description || '桌面小组件'}</p>
                    </div>
                  </div>

                  {/* Status Pill */}
                  <span className={`px-2 py-0.5 rounded-full text-[10px] font-semibold shrink-0 ${
                    w.running 
                      ? 'bg-emerald-50 dark:bg-emerald-950/80 text-emerald-700 dark:text-emerald-300 border border-emerald-200 dark:border-emerald-800/60' 
                      : 'bg-slate-100 dark:bg-slate-800 text-slate-600 dark:text-slate-400 border border-slate-200 dark:border-slate-700/50'
                  }`}>
                    {w.running ? '运行中' : '未启动'}
                  </span>
                </div>
              </div>

              {/* Inline Settings Drawer */}
              {isExpanded && hasSettings && (
                <div className="px-4 pb-4 pt-2 border-t border-slate-200/80 dark:border-slate-800/80 bg-slate-50 dark:bg-slate-900/60 space-y-3">
                  <div className="flex items-center justify-between text-xs font-bold text-slate-800 dark:text-slate-200">
                    <span>组件设置</span>
                    {saveSuccessId === w.id && (
                      <span className="text-[11px] text-emerald-600 dark:text-emerald-400 font-semibold animate-pulse">已保存</span>
                    )}
                  </div>

                  <div className="space-y-2.5">
                    {w.settingsSchema?.map((field: WidgetSettingField) => {
                      const currentVal = formValuesMap[w.id]?.[field.key] ?? field.default ?? '';

                      return (
                        <div key={field.key} className="space-y-1">
                          <label className="text-[11px] font-medium text-slate-700 dark:text-slate-300 block">
                            {field.title || field.key}
                          </label>

                          {field.type === 'string' && (
                            <input
                              type="text"
                              value={currentVal}
                              onChange={(e) => handleFieldChange(w.id, field.key, e.target.value)}
                              className="w-full px-2.5 py-1 text-xs rounded-lg border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-1 focus:ring-indigo-500 shadow-2xs"
                            />
                          )}

                          {field.type === 'number' && (
                            <input
                              type="number"
                              min={field.min}
                              max={field.max}
                              step={field.step || 1}
                              value={currentVal}
                              onChange={(e) => handleFieldChange(w.id, field.key, parseFloat(e.target.value))}
                              className="w-full px-2.5 py-1 text-xs rounded-lg border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-1 focus:ring-indigo-500 shadow-2xs"
                            />
                          )}

                          {field.type === 'boolean' && (
                            <button
                              type="button"
                              onClick={() => handleFieldChange(w.id, field.key, !currentVal)}
                              className={`relative inline-flex h-4.5 w-8 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ${
                                currentVal ? 'bg-indigo-600' : 'bg-slate-300 dark:bg-slate-700'
                              }`}
                            >
                              <span className={`inline-block h-3.5 w-3.5 transform rounded-full bg-white shadow-xs transition duration-200 ${
                                currentVal ? 'translate-x-3.5' : 'translate-x-0'
                              }`} />
                            </button>
                          )}
                        </div>
                      );
                    })}
                  </div>

                  <div className="flex items-center justify-end gap-2 pt-2 border-t border-slate-200/60 dark:border-slate-800/60">
                    <button
                      onClick={() => handleResetDefaults(w)}
                      className="px-2.5 py-1 text-[11px] font-medium text-slate-500 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 flex items-center gap-1 cursor-pointer"
                    >
                      <RotateCcw className="w-3 h-3" />
                      <span>恢复默认</span>
                    </button>
                    <button
                      onClick={() => handleSaveSettingsClick(w)}
                      className="px-3 py-1 text-[11px] font-semibold bg-indigo-600 hover:bg-indigo-500 text-white rounded-lg flex items-center gap-1 shadow-xs cursor-pointer"
                    >
                      <Save className="w-3 h-3" />
                      <span>保存</span>
                    </button>
                  </div>
                </div>
              )}

              {/* Action Toolbar */}
              <div className="px-4 py-2.5 bg-slate-50/70 dark:bg-[#0f1422] border-t border-slate-200/80 dark:border-slate-800/80 flex items-center justify-between gap-2">
                <div className="flex items-center gap-2">
                  {w.running ? (
                    <button
                      onClick={() => onStop(w.id)}
                      className="px-3 py-1.5 rounded-lg text-xs font-semibold bg-white dark:bg-slate-800 hover:bg-slate-100 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-200 border border-slate-200 dark:border-transparent flex items-center gap-1.5 shadow-2xs transition-colors cursor-pointer"
                    >
                      <Square className="w-3.5 h-3.5 text-rose-600 dark:text-rose-400 fill-rose-600 dark:fill-rose-400" />
                      <span>停止</span>
                    </button>
                  ) : (
                    <button
                      onClick={() => onLaunch(w.id, w.packagePath)}
                      className="px-3 py-1.5 rounded-lg text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white flex items-center gap-1.5 shadow-xs transition-colors active:scale-95 cursor-pointer"
                    >
                      <Play className="w-3.5 h-3.5 fill-white" />
                      <span>启动</span>
                    </button>
                  )}

                  {hasSettings && (
                    <button
                      onClick={() => handleToggleSettings(w)}
                      className={`px-2.5 py-1.5 rounded-lg text-xs font-medium border flex items-center gap-1 transition-colors cursor-pointer ${
                        isExpanded
                          ? 'bg-indigo-50 dark:bg-indigo-950/80 border-indigo-200 dark:border-indigo-800/80 text-indigo-700 dark:text-indigo-300'
                          : 'bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-800 text-slate-700 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800 shadow-2xs'
                      }`}
                    >
                      <SlidersHorizontal className="w-3 h-3" />
                      <span>配置</span>
                    </button>
                  )}
                </div>

                <div className="flex items-center gap-1">
                  <button
                    onClick={() => onOpenFolder(w.id, w.packagePath)}
                    className="p-1.5 text-slate-400 hover:text-slate-700 dark:hover:text-slate-200 hover:bg-slate-200/60 dark:hover:bg-slate-800 rounded-lg transition-colors cursor-pointer"
                    title="打开组件目录"
                  >
                    <FolderOpen className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={() => onUninstall(w.id, w.packagePath, w.name)}
                    className="p-1.5 text-slate-400 hover:text-rose-600 dark:hover:text-rose-400 hover:bg-slate-200/60 dark:hover:bg-slate-800 rounded-lg transition-colors cursor-pointer"
                    title="卸载组件"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>
            </div>
          );
        })}
      </div>
      )}
    </div>
  );
};
