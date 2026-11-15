import React, { useState, useEffect, useRef } from 'react';
import {
  X,
  RotateCcw,
  Save,
  Check,
  FolderOpen,
  ChevronDown,
  Clock,
  CheckSquare,
  StickyNote,
  Activity,
  Timer,
  Droplets,
  CloudSun,
  Code,
  Layers,
  Settings2,
} from 'lucide-react';
import { WidgetInfo, WidgetSettingField } from '../types';

interface UnifiedWidgetSettingsModalProps {
  widget: WidgetInfo | null;
  isOpen: boolean;
  onClose: () => void;
  onSaveSettings: (widgetId: string, settings: Record<string, any>) => void;
  onRestart?: (widgetId: string) => void;
  onOpenFolder?: (widgetId: string, packagePath: string) => void;
}

// 提取图标与主题映射
function getWidgetVisual(widgetId?: string, isDev?: boolean) {
  const norm = (widgetId || '').toLowerCase();
  if (norm.includes('clock')) {
    return {
      icon: Clock,
      color: 'text-indigo-600 dark:text-indigo-400',
      bg: 'bg-indigo-50 border-indigo-200/80 dark:bg-indigo-950/60 dark:border-indigo-800/80',
    };
  }
  if (norm.includes('todo')) {
    return {
      icon: CheckSquare,
      color: 'text-emerald-600 dark:text-emerald-400',
      bg: 'bg-emerald-50 border-emerald-200/80 dark:bg-emerald-950/60 dark:border-emerald-800/80',
    };
  }
  if (norm.includes('notes')) {
    return {
      icon: StickyNote,
      color: 'text-amber-600 dark:text-amber-400',
      bg: 'bg-amber-50 border-amber-200/80 dark:bg-amber-950/60 dark:border-amber-800/80',
    };
  }
  if (norm.includes('sysmonitor') || norm.includes('system')) {
    return {
      icon: Activity,
      color: 'text-blue-600 dark:text-blue-400',
      bg: 'bg-blue-50 border-blue-200/80 dark:bg-blue-950/60 dark:border-blue-800/80',
    };
  }
  if (norm.includes('timer')) {
    return {
      icon: Timer,
      color: 'text-purple-600 dark:text-purple-400',
      bg: 'bg-purple-50 border-purple-200/80 dark:bg-purple-950/60 dark:border-purple-800/80',
    };
  }
  if (norm.includes('water')) {
    return {
      icon: Droplets,
      color: 'text-cyan-600 dark:text-cyan-400',
      bg: 'bg-cyan-50 border-cyan-200/80 dark:bg-cyan-950/60 dark:border-cyan-800/80',
    };
  }
  if (norm.includes('weather')) {
    return {
      icon: CloudSun,
      color: 'text-sky-600 dark:text-sky-400',
      bg: 'bg-sky-50 border-sky-200/80 dark:bg-sky-950/60 dark:border-sky-800/80',
    };
  }
  return {
    icon: isDev ? Code : Layers,
    color: isDev ? 'text-amber-600 dark:text-amber-400' : 'text-slate-600 dark:text-slate-400',
    bg: isDev
      ? 'bg-amber-50 border-amber-200/80 dark:bg-amber-950/60 dark:border-amber-800/80'
      : 'bg-slate-100 border-slate-200 dark:bg-slate-900 dark:border-slate-800',
  };
}

function ModalWidgetIcon({ widget }: { widget: WidgetInfo }) {
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
}

// 统一可下拉控件，具备外溢自适应与高层级展开
function ModalSelect({
  value,
  options,
  onChange,
}: {
  value: any;
  options?: Array<{ label: string; value: any }>;
  onChange: (val: any) => void;
}) {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (containerRef.current && !containerRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    }
    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside);
      return () => document.removeEventListener('mousedown', handleClickOutside);
    }
  }, [isOpen]);

  const opts = options || [];
  const currentOption = opts.find((o) => String(o.value) === String(value));

  return (
    <div className="relative w-full min-w-[200px]" ref={containerRef}>
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        className="w-full px-3 py-1.5 text-xs rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 flex items-center justify-between shadow-2xs hover:border-slate-300 dark:hover:border-slate-700 transition-colors cursor-pointer"
      >
        <span className="truncate">{currentOption ? currentOption.label : String(value || '请选择')}</span>
        <ChevronDown className={`w-3.5 h-3.5 text-slate-400 transition-transform ${isOpen ? 'rotate-180' : ''}`} />
      </button>

      {isOpen && (
        <div className="absolute z-50 left-0 right-0 mt-1.5 max-h-56 overflow-y-auto rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 shadow-xl py-1">
          {opts.map((opt) => {
            const isSelected = String(value) === String(opt.value);
            return (
              <button
                key={String(opt.value)}
                type="button"
                onClick={() => {
                  onChange(opt.value);
                  setIsOpen(false);
                }}
                className={`w-full px-3 py-2 text-xs text-left flex items-center justify-between cursor-pointer transition-colors ${
                  isSelected
                    ? 'bg-indigo-50 dark:bg-indigo-950/60 text-indigo-600 dark:text-indigo-400 font-semibold'
                    : 'hover:bg-slate-100 dark:hover:bg-slate-900 text-slate-700 dark:text-slate-300'
                }`}
              >
                <span className="truncate">{opt.label}</span>
                {isSelected && <Check className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400 shrink-0 ml-1" />}
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
}

function normalizeSchema(schema: any): WidgetSettingField[] {
  if (!schema) return [];
  if (Array.isArray(schema)) return schema;
  if (typeof schema === 'object') {
    return Object.entries(schema).map(([key, val]: [string, any]) => ({
      key,
      type: val?.type || 'string',
      title: val?.title || val?.label || key,
      description: val?.description,
      default: val?.default,
      min: val?.min,
      max: val?.max,
      step: val?.step,
      options: val?.options,
    }));
  }
  return [];
}

export const UnifiedWidgetSettingsModal: React.FC<UnifiedWidgetSettingsModalProps> = ({
  widget,
  isOpen,
  onClose,
  onSaveSettings,
  onRestart,
  onOpenFolder,
}) => {
  const [formValues, setFormValues] = useState<Record<string, any>>({});
  const [isSaved, setIsSaved] = useState(false);
  const [restarted, setRestarted] = useState(false);

  // 初始化当前组件配置项
  useEffect(() => {
    if (!widget) {
      setFormValues({});
      setIsSaved(false);
      setRestarted(false);
      return;
    }

    const initial: Record<string, any> = { ...(widget.settingsValues || {}) };
    const fields = normalizeSchema(widget.settingsSchema);
    fields.forEach((field) => {
      if (initial[field.key] === undefined && field.default !== undefined) {
        initial[field.key] = field.default;
      }
    });
    setFormValues(initial);
    setIsSaved(false);
    setRestarted(false);
  }, [widget]);

  // 锁定底座背景滚动，防止鼠标滚轮穿透滚动 Hub 页面
  useEffect(() => {
    if (!isOpen) return;

    const prevBodyOverflow = document.body.style.overflow;
    const prevDocOverflow = document.documentElement.style.overflow;
    const prevOverscroll = document.body.style.overscrollBehavior;

    document.body.style.overflow = 'hidden';
    document.documentElement.style.overflow = 'hidden';
    document.body.style.overscrollBehavior = 'none';

    return () => {
      document.body.style.overflow = prevBodyOverflow;
      document.documentElement.style.overflow = prevDocOverflow;
      document.body.style.overscrollBehavior = prevOverscroll;
    };
  }, [isOpen]);

  // ESC 全局按键监听
  useEffect(() => {
    if (!isOpen) return;
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        onClose();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, onClose]);

  if (!isOpen || !widget) return null;

  // 组件配置统一归一为两大类型：自定义与参数型配置
  const isCustomType = Boolean(widget.settingsUrl);
  const schemaFields = normalizeSchema(widget.settingsSchema);
  const hasSchema = schemaFields.length > 0;

  const handleFieldChange = (key: string, val: any) => {
    setFormValues((prev) => ({ ...prev, [key]: val }));
    setIsSaved(false);
  };

  const handleResetDefaults = () => {
    const defaults: Record<string, any> = {};
    schemaFields.forEach((field) => {
      if (field.default !== undefined) {
        defaults[field.key] = field.default;
      }
    });
    setFormValues(defaults);
    setIsSaved(false);
  };

  const handleSave = () => {
    onSaveSettings(widget.id, formValues);
    setIsSaved(true);
    setTimeout(() => {
      setIsSaved(false);
    }, 2500);
  };

  const handleRestartClick = () => {
    if (onRestart && widget.id) {
      onRestart(widget.id);
      setRestarted(true);
      setTimeout(() => setRestarted(false), 2000);
    }
  };

  return (
    <div
      className="fixed inset-0 z-[100] flex items-center justify-center bg-slate-950/60 dark:bg-black/75 backdrop-blur-sm p-4 md:p-6 overscroll-contain animate-in fade-in duration-150"
      onClick={onClose}
      onWheel={(e) => e.stopPropagation()}
    >
      <div
        className={`w-[95vw] bg-white dark:bg-[#131927] border border-slate-200/90 dark:border-slate-800 rounded-2xl shadow-2xl overflow-hidden flex flex-col overscroll-contain animate-in zoom-in-95 duration-150 transition-all ${
          isCustomType
            ? 'max-w-2xl md:max-w-3xl lg:max-w-4xl xl:max-w-5xl 2xl:max-w-6xl h-[84vh] max-h-[920px] min-h-[540px]'
            : 'max-w-2xl md:max-w-3xl lg:max-w-4xl max-h-[86vh] min-h-[420px]'
        }`}
        onClick={(e) => e.stopPropagation()}
        onWheel={(e) => e.stopPropagation()}
      >
        {/* 顶部标题栏 */}
        <div className="px-5 py-3.5 border-b border-slate-200/80 dark:border-slate-800/80 bg-slate-50/80 dark:bg-[#0f1422] flex items-center justify-between gap-3 shrink-0">
          <div className="flex items-center gap-3 min-w-0">
            <ModalWidgetIcon widget={widget} />
            <div className="min-w-0">
              <div className="flex items-center gap-2">
                <h3 className="text-sm font-bold text-slate-900 dark:text-slate-100 truncate">
                  {widget.name} · 组件配置
                </h3>
                {isCustomType ? (
                  <span className="px-2 py-0.5 rounded-md text-[10px] font-semibold bg-indigo-50 dark:bg-indigo-950/70 text-indigo-600 dark:text-indigo-400 border border-indigo-200/60 dark:border-indigo-800/60 shrink-0">
                    自定义
                  </span>
                ) : (
                  <span className="px-2 py-0.5 rounded-md text-[10px] font-semibold bg-emerald-50 dark:bg-emerald-950/70 text-emerald-600 dark:text-emerald-400 border border-emerald-200/60 dark:border-emerald-800/60 shrink-0">
                    参数型配置
                  </span>
                )}
              </div>
              <p className="text-[11px] text-slate-500 dark:text-slate-400 truncate mt-0.5">
                版本 {widget.version || '1.0.0'} · 作者 {widget.author || 'LinDesk'}
              </p>
            </div>
          </div>

          {/* 右侧操作按钮 */}
          <div className="flex items-center gap-2 shrink-0">
            {widget.running && onRestart && (
              <button
                type="button"
                onClick={handleRestartClick}
                className="px-2.5 py-1.5 rounded-lg text-xs font-medium bg-white dark:bg-slate-800 hover:bg-slate-100 dark:hover:bg-slate-700 text-slate-700 dark:text-slate-200 border border-slate-200 dark:border-slate-700 flex items-center gap-1.5 shadow-2xs transition-colors cursor-pointer"
                title="重新载入桌面正在运行的该组件实例"
              >
                <RotateCcw className={`w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400 ${restarted ? 'animate-spin' : ''}`} />
                <span>{restarted ? '已重新加载' : '重新加载'}</span>
              </button>
            )}

            {onOpenFolder && widget.packagePath && (
              <button
                type="button"
                onClick={() => onOpenFolder(widget.id, widget.packagePath)}
                className="p-1.5 rounded-lg text-slate-400 hover:text-slate-700 dark:hover:text-slate-200 hover:bg-slate-200/60 dark:hover:bg-slate-800 transition-colors cursor-pointer"
                title="打开组件本地存储目录"
              >
                <FolderOpen className="w-4 h-4" />
              </button>
            )}

            <button
              type="button"
              onClick={onClose}
              className="p-1.5 rounded-lg text-slate-400 hover:text-slate-700 dark:hover:text-slate-200 hover:bg-slate-200/60 dark:hover:bg-slate-800 transition-colors cursor-pointer"
              title="关闭配置窗口 (ESC)"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        </div>

        {/* 核心内容区：按自定义与参数型配置两类分别呈现 */}
        {isCustomType ? (
          <div
            className="flex-1 w-full flex flex-col p-3 sm:p-5 h-full overflow-hidden"
            onWheel={(e) => e.stopPropagation()}
          >
            <div className="flex-1 w-full rounded-xl border border-slate-200/80 dark:border-slate-800/80 overflow-hidden bg-slate-50 dark:bg-[#0c101b] shadow-inner flex flex-col">
              <iframe
                src={widget.settingsUrl}
                title={`${widget.name} 自定义配置面板`}
                className="w-full h-full flex-1 border-0 block"
              />
            </div>
          </div>
        ) : (
          <div
            className="flex-1 overflow-y-auto px-6 py-5 min-h-[320px] max-h-[68vh] overscroll-contain space-y-4"
            onWheel={(e) => e.stopPropagation()}
          >
            {hasSchema ? (
              <div className="space-y-3.5">
                {schemaFields.map((field) => {
                  const currentVal = formValues[field.key] ?? field.default ?? '';

                  return (
                    <div
                      key={field.key}
                      className="p-4 rounded-xl border border-slate-200/90 dark:border-slate-800/90 bg-slate-50/50 dark:bg-slate-900/40 hover:bg-slate-50 dark:hover:bg-slate-900/60 transition-colors flex flex-col sm:flex-row sm:items-center justify-between gap-3"
                    >
                      <div className="min-w-0 pr-2">
                        <div className="flex items-center gap-2">
                          <label className="text-xs font-bold text-slate-900 dark:text-slate-100 block">
                            {field.title || field.key}
                          </label>
                          <span className="text-[10px] text-slate-400 font-mono">
                            {field.key}
                          </span>
                        </div>
                        {field.description && (
                          <p className="text-[11px] text-slate-500 dark:text-slate-400 mt-1 leading-relaxed">
                            {field.description}
                          </p>
                        )}
                      </div>

                      <div className="shrink-0 flex items-center justify-end sm:w-auto">
                        {field.type === 'string' && (
                          <input
                            type="text"
                            value={currentVal}
                            onChange={(e) => handleFieldChange(field.key, e.target.value)}
                            className="w-full sm:w-60 px-3 py-1.5 text-xs rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-1 focus:ring-indigo-500 shadow-2xs"
                          />
                        )}

                        {field.type === 'number' && (
                          <div className="flex items-center gap-2">
                            <input
                              type="number"
                              min={field.min}
                              max={field.max}
                              step={field.step || 1}
                              value={currentVal}
                              onChange={(e) => handleFieldChange(field.key, parseFloat(e.target.value))}
                              className="w-24 px-3 py-1.5 text-xs rounded-xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 text-slate-800 dark:text-slate-100 focus:outline-none focus:ring-1 focus:ring-indigo-500 shadow-2xs text-center"
                            />
                            {field.min !== undefined && field.max !== undefined && (
                              <span className="text-[11px] text-slate-400 font-mono">
                                [{field.min} ~ {field.max}]
                              </span>
                            )}
                          </div>
                        )}

                        {field.type === 'select' && (
                          <ModalSelect
                            value={currentVal}
                            options={field.options}
                            onChange={(val) => handleFieldChange(field.key, val)}
                          />
                        )}

                        {field.type === 'color' && (
                          <div className="flex items-center gap-2">
                            <input
                              type="color"
                              value={currentVal || '#6366f1'}
                              onChange={(e) => handleFieldChange(field.key, e.target.value)}
                              className="h-8 w-14 rounded-lg border border-slate-200 dark:border-slate-800 bg-white dark:bg-slate-950 cursor-pointer p-0.5"
                            />
                            <span className="text-xs font-mono text-slate-600 dark:text-slate-400 uppercase">
                              {currentVal || '#6366f1'}
                            </span>
                          </div>
                        )}

                        {field.type === 'boolean' && (
                          <button
                            type="button"
                            onClick={() => handleFieldChange(field.key, !currentVal)}
                            className={`relative inline-flex h-5 w-9 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ${
                              currentVal ? 'bg-indigo-600' : 'bg-slate-300 dark:bg-slate-700'
                            }`}
                          >
                            <span
                              className={`inline-block h-4 w-4 transform rounded-full bg-white shadow-xs transition duration-200 ${
                                currentVal ? 'translate-x-4' : 'translate-x-0'
                              }`}
                            />
                          </button>
                        )}
                      </div>
                    </div>
                  );
                })}
              </div>
            ) : (
              <div className="py-16 text-center">
                <Settings2 className="w-10 h-10 text-slate-400 dark:text-slate-600 mx-auto mb-2 opacity-60" />
                <p className="text-xs font-bold text-slate-700 dark:text-slate-300">
                  此小组件无需单独配置
                </p>
                <p className="text-[11px] text-slate-400 dark:text-slate-500 mt-1">
                  所有样式与功能均按出厂最佳参数常驻桌面运行
                </p>
              </div>
            )}
          </div>
        )}

        {/* 底部操作栏 */}
        <div className="px-6 py-3 border-t border-slate-200/80 dark:border-slate-800/80 bg-slate-50/80 dark:bg-[#0f1422] flex items-center justify-between shrink-0">
          <div className="flex items-center gap-2">
            {!isCustomType && hasSchema && (
              <button
                type="button"
                onClick={handleResetDefaults}
                className="px-3 py-1.5 text-xs font-medium text-slate-600 dark:text-slate-400 hover:text-slate-900 dark:hover:text-slate-100 flex items-center gap-1.5 cursor-pointer rounded-lg hover:bg-slate-200/60 dark:hover:bg-slate-800 transition-colors"
              >
                <RotateCcw className="w-3.5 h-3.5" />
                <span>恢复默认</span>
              </button>
            )}

            {!isCustomType && isSaved && (
              <div className="flex items-center gap-1 text-xs font-semibold text-emerald-600 dark:text-emerald-400 animate-in fade-in duration-200">
                <Check className="w-3.5 h-3.5" />
                <span>配置已保存并同步至桌面</span>
              </div>
            )}

            {isCustomType && (
              <span className="text-[11px] text-slate-400 dark:text-slate-500">
                自定义配置由组件面板独立管理
              </span>
            )}
          </div>

          <div className="flex items-center gap-2.5">
            <button
              type="button"
              onClick={onClose}
              className="px-3.5 py-1.5 text-xs font-medium text-slate-700 dark:text-slate-300 bg-white dark:bg-slate-800 hover:bg-slate-100 dark:hover:bg-slate-700 border border-slate-200 dark:border-slate-700 rounded-xl transition-colors cursor-pointer shadow-2xs"
            >
              关闭
            </button>

            {!isCustomType && hasSchema && (
              <button
                type="button"
                onClick={handleSave}
                className="px-4 py-1.5 text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white rounded-xl flex items-center gap-1.5 shadow-xs transition-colors cursor-pointer"
              >
                <Save className="w-3.5 h-3.5" />
                <span>保存并应用</span>
              </button>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
