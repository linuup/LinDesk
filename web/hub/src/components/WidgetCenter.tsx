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
  RotateCcw 
} from 'lucide-react';

interface WidgetCenterProps {
  widgets: WidgetInfo[];
  runtimeEnabled: boolean;
  showBounds?: boolean;
  onToggleMaster: (enabled: boolean) => void;
  onToggleBounds?: (enabled: boolean) => void;
  onLaunch: (id: string, packagePath: string) => void;
  onStop: (id: string) => void;
  onRestart: (id: string) => void;
  onTogglePermission: (id: string, perm: string, granted: boolean) => void;
  onClearStorage: (id: string, name: string) => void;
  onOpenFolder: (id: string, path: string) => void;
  onOpenWidgetsDir: () => void;
  onShowDevTools: (id: string) => void;
  onUninstall: (id: string, name: string) => void;
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
      return { icon: Clock, color: 'text-indigo-600', bg: 'bg-indigo-50 border-indigo-200/90' };
    }
    if (norm.includes('todo')) {
      return { icon: CheckSquare, color: 'text-emerald-600', bg: 'bg-emerald-50 border-emerald-200/90' };
    }
    if (norm.includes('notes')) {
      return { icon: StickyNote, color: 'text-amber-600', bg: 'bg-amber-50 border-amber-200/90' };
    }
    if (norm.includes('sysmonitor') || norm.includes('system')) {
      return { icon: Activity, color: 'text-blue-600', bg: 'bg-blue-50 border-blue-200/90' };
    }
    if (norm.includes('timer')) {
      return { icon: Timer, color: 'text-purple-600', bg: 'bg-purple-50 border-purple-200/90' };
    }
    if (norm.includes('water')) {
      return { icon: Droplets, color: 'text-cyan-600', bg: 'bg-cyan-50 border-cyan-200/90' };
    }
    if (norm.includes('weather')) {
      return { icon: CloudSun, color: 'text-sky-600', bg: 'bg-sky-50 border-sky-200/90' };
    }
    return {
      icon: isDev ? Code : Layers,
      color: isDev ? 'text-amber-600' : 'text-slate-600',
      bg: isDev ? 'bg-amber-50 border-amber-200/90' : 'bg-slate-100 border-slate-200/90',
    };
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
      <div className="bg-white rounded-2xl p-4 border border-slate-200/80 shadow-2xs space-y-3.5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          {/* Master Toggle */}
          <div className="flex items-center gap-3">
            <div className="flex items-center gap-2.5 bg-slate-50 border border-slate-200/80 px-3.5 py-1.5 rounded-xl shadow-2xs">
              <span className="text-xs font-semibold text-slate-700">桌面小组件总开关:</span>
              <button
                type="button"
                onClick={() => onToggleMaster(!runtimeEnabled)}
                className={`relative inline-flex h-5 w-9 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ease-in-out focus:outline-none ${
                  runtimeEnabled ? 'bg-indigo-600' : 'bg-slate-300'
                }`}
                title={runtimeEnabled ? '点击关闭所有已加载的桌面小组件（不影响动态壁纸）' : '点击开启所有已加载的桌面小组件'}
              >
                <span
                  className={`pointer-events-none inline-block h-4 w-4 transform rounded-full bg-white shadow-xs ring-0 transition duration-200 ease-in-out ${
                    runtimeEnabled ? 'translate-x-4' : 'translate-x-0'
                  }`}
                />
              </button>
              <span className={`text-[11px] font-bold ${runtimeEnabled ? 'text-indigo-600' : 'text-slate-400'}`}>
                {runtimeEnabled ? '已开启' : '已关闭'}
              </span>
              <span className="text-[11px] text-slate-400 hidden sm:inline">
                (仅控制小组件，不含壁纸)
              </span>
            </div>
          </div>

          {/* Quick Actions */}
          <div className="flex items-center gap-2">
            <button
              onClick={onOpenCreateModal}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-700 text-white flex items-center gap-1.5 shadow-2xs transition-all active:scale-95"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>新建组件</span>
            </button>
            <button
              onClick={onImportDevProject}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-white hover:bg-slate-50 text-slate-700 border border-slate-200 flex items-center gap-1.5 shadow-2xs transition-all active:scale-95"
            >
              <Code className="w-3.5 h-3.5 text-amber-500" />
              <span>导入本地组件</span>
            </button>
            <button
              onClick={onOpenWidgetsDir}
              className="px-3.5 py-1.5 rounded-xl text-xs font-semibold bg-white hover:bg-slate-50 text-slate-700 border border-slate-200 flex items-center gap-1.5 shadow-2xs transition-all active:scale-95"
            >
              <FolderOpen className="w-3.5 h-3.5 text-slate-500" />
              <span>打开文件夹</span>
            </button>
          </div>
        </div>

        {/* Filter Pills & Search */}
        <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-3 pt-3 border-t border-slate-100">
          <div className="flex items-center gap-1.5">
            <button
              onClick={() => setFilterMode('all')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all ${
                filterMode === 'all'
                  ? 'bg-indigo-600 text-white font-semibold shadow-2xs'
                  : 'bg-slate-100 text-slate-600 hover:bg-slate-200'
              }`}
            >
              全部 ({totalCount})
            </button>
            <button
              onClick={() => setFilterMode('running')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all ${
                filterMode === 'running'
                  ? 'bg-emerald-600 text-white font-semibold shadow-2xs'
                  : 'bg-slate-100 text-slate-600 hover:bg-slate-200'
              }`}
            >
              运行中 ({runningCount})
            </button>
            <button
              onClick={() => setFilterMode('official')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all ${
                filterMode === 'official'
                  ? 'bg-slate-800 text-white font-semibold shadow-2xs'
                  : 'bg-slate-100 text-slate-600 hover:bg-slate-200'
              }`}
            >
              官方内置
            </button>
            <button
              onClick={() => setFilterMode('dev')}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-all ${
                filterMode === 'dev'
                  ? 'bg-amber-600 text-white font-semibold shadow-2xs'
                  : 'bg-slate-100 text-slate-600 hover:bg-slate-200'
              }`}
            >
              自定义
            </button>
          </div>

          <div className="relative">
            <Search className="w-3.5 h-3.5 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="搜索组件..."
              className="pl-8 pr-3 py-1.5 rounded-xl text-xs bg-slate-50 border border-slate-200 text-slate-800 focus:outline-none focus:ring-2 focus:ring-indigo-500/15 focus:border-indigo-500 transition-all placeholder:text-slate-400 w-full sm:w-56"
            />
          </div>
        </div>
      </div>

      {/* 2. Compact Modern Widget Cards Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {filteredWidgets.map((w) => {
          const visual = getWidgetVisual(w.id, w.isDevelopment);
          const IconComp = visual.icon;
          const isExpanded = expandedSettingsId === w.id;
          const hasSettings = w.hasSettings || (w.settingsSchema && w.settingsSchema.length > 0);

          return (
            <div
              key={w.id}
              className={`rounded-2xl border transition-all duration-150 bg-white shadow-2xs flex flex-col justify-between overflow-hidden ${
                w.running ? 'border-indigo-500/40' : 'border-slate-200/80 hover:border-slate-300'
              }`}
            >
              {/* Card Header & Body */}
              <div className="p-4 space-y-3">
                <div className="flex items-start justify-between gap-3">
                  <div className="flex items-center gap-3 min-w-0">
                    <div className={`w-10 h-10 rounded-xl ${visual.bg} flex items-center justify-center shrink-0 border`}>
                      <IconComp className={`w-5 h-5 ${visual.color}`} />
                    </div>
                    <div className="min-w-0">
                      <div className="flex items-center gap-2">
                        <h4 className="text-xs font-bold text-slate-900 truncate">{w.name}</h4>
                        <span className="text-[10px] text-slate-400 font-mono">{w.version || '1.0'}</span>
                      </div>
                      <p className="text-[11px] text-slate-500 line-clamp-1 mt-0.5">{w.description || '桌面小组件'}</p>
                    </div>
                  </div>

                  {/* Status Pill */}
                  <span className={`px-2 py-0.5 rounded-full text-[10px] font-semibold shrink-0 ${
                    w.running 
                      ? 'bg-emerald-50 text-emerald-700 border border-emerald-200/80' 
                      : 'bg-slate-100 text-slate-500'
                  }`}>
                    {w.running ? '运行中' : '未启动'}
                  </span>
                </div>
              </div>

              {/* Inline Settings Drawer */}
              {isExpanded && hasSettings && (
                <div className="px-4 pb-4 pt-2 border-t border-slate-100 bg-slate-50/50 space-y-3">
                  <div className="flex items-center justify-between text-xs font-bold text-slate-800">
                    <span>组件设置</span>
                    {saveSuccessId === w.id && (
                      <span className="text-[11px] text-emerald-600 font-semibold animate-pulse">已保存</span>
                    )}
                  </div>

                  <div className="space-y-2.5">
                    {w.settingsSchema?.map((field: WidgetSettingField) => {
                      const currentVal = formValuesMap[w.id]?.[field.key] ?? field.default ?? '';

                      return (
                        <div key={field.key} className="space-y-1">
                          <label className="text-[11px] font-medium text-slate-600 block">
                            {field.title || field.key}
                          </label>

                          {field.type === 'string' && (
                            <input
                              type="text"
                              value={currentVal}
                              onChange={(e) => handleFieldChange(w.id, field.key, e.target.value)}
                              className="w-full px-2.5 py-1 text-xs rounded-lg border border-slate-200 bg-white text-slate-800 focus:outline-none focus:ring-1 focus:ring-indigo-500"
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
                              className="w-full px-2.5 py-1 text-xs rounded-lg border border-slate-200 bg-white text-slate-800 focus:outline-none focus:ring-1 focus:ring-indigo-500"
                            />
                          )}

                          {field.type === 'boolean' && (
                            <button
                              type="button"
                              onClick={() => handleFieldChange(w.id, field.key, !currentVal)}
                              className={`relative inline-flex h-4.5 w-8 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ${
                                currentVal ? 'bg-indigo-600' : 'bg-slate-300'
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

                  <div className="flex items-center justify-end gap-2 pt-2 border-t border-slate-200/60">
                    <button
                      onClick={() => handleResetDefaults(w)}
                      className="px-2.5 py-1 text-[11px] font-medium text-slate-500 hover:text-slate-800 flex items-center gap-1"
                    >
                      <RotateCcw className="w-3 h-3" />
                      <span>恢复默认</span>
                    </button>
                    <button
                      onClick={() => handleSaveSettingsClick(w)}
                      className="px-3 py-1 text-[11px] font-semibold bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg flex items-center gap-1 shadow-2xs"
                    >
                      <Save className="w-3 h-3" />
                      <span>保存</span>
                    </button>
                  </div>
                </div>
              )}

              {/* Action Toolbar */}
              <div className="px-4 py-2.5 bg-slate-50/80 border-t border-slate-100 flex items-center justify-between gap-2">
                <div className="flex items-center gap-2">
                  {w.running ? (
                    <button
                      onClick={() => onStop(w.id)}
                      className="px-3 py-1.5 rounded-lg text-xs font-semibold bg-slate-200/80 hover:bg-slate-300 text-slate-700 flex items-center gap-1.5 transition-colors"
                    >
                      <Square className="w-3 h-3 text-red-500 fill-red-500" />
                      <span>停止</span>
                    </button>
                  ) : (
                    <button
                      onClick={() => onLaunch(w.id, w.packagePath)}
                      className="px-3 py-1.5 rounded-lg text-xs font-semibold bg-indigo-600 hover:bg-indigo-700 text-white flex items-center gap-1.5 shadow-2xs transition-colors active:scale-95"
                    >
                      <Play className="w-3 h-3 fill-white" />
                      <span>启动</span>
                    </button>
                  )}

                  {hasSettings && (
                    <button
                      onClick={() => handleToggleSettings(w)}
                      className={`px-2.5 py-1.5 rounded-lg text-xs font-medium border flex items-center gap-1 transition-colors ${
                        isExpanded
                          ? 'bg-indigo-50 border-indigo-200 text-indigo-700'
                          : 'bg-white border-slate-200 text-slate-600 hover:bg-slate-100'
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
                    className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-white rounded-lg transition-colors"
                    title="打开组件目录"
                  >
                    <FolderOpen className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={() => onUninstall(w.id, w.name)}
                    className="p-1.5 text-slate-400 hover:text-rose-600 hover:bg-white rounded-lg transition-colors"
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
    </div>
  );
};
