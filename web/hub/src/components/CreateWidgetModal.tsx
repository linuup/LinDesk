import React, { useState } from 'react';
import { X, Sparkles, Code2, Layers, Cpu } from 'lucide-react';
import { CreateWidgetData } from '../types';

interface CreateWidgetModalProps {
  isOpen: boolean;
  onClose: () => void;
  onCreate: (data: CreateWidgetData) => void;
}

export const CreateWidgetModal: React.FC<CreateWidgetModalProps> = ({
  isOpen,
  onClose,
  onCreate
}) => {
  const [name, setName] = useState('');
  const [widgetId, setWidgetId] = useState('');
  const [author, setAuthor] = useState('LinDesk Developer');
  const [description, setDescription] = useState('');
  const [template, setTemplate] = useState<'vanilla' | 'react' | 'webgl'>('vanilla');
  const [width, setWidth] = useState(320);
  const [height, setHeight] = useState(240);

  if (!isOpen) return null;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim() || !widgetId.trim()) return;

    onCreate({
      name: name.trim(),
      id: widgetId.trim(),
      author: author.trim(),
      description: description.trim(),
      template,
      width: Number(width) || 320,
      height: Number(height) || 240
    });
  };

  const handleNameChange = (val: string) => {
    setName(val);
    if (!widgetId || widgetId.startsWith('com.lindesk.')) {
      const slug = val
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-|-$/g, '');
      setWidgetId(`com.lindesk.${slug || 'newwidget'}`);
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 dark:bg-black/75 backdrop-blur-md animate-in fade-in duration-150">
      <div 
        className="relative w-full max-w-lg rounded-2xl border border-slate-200 dark:border-slate-800 bg-white dark:bg-[#131927] p-6 shadow-2xl text-slate-800 dark:text-slate-100 animate-in zoom-in-95 duration-150"
        onClick={e => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between pb-4 border-b border-slate-200/80 dark:border-slate-800">
          <div className="flex items-center gap-2.5">
            <div className="w-8 h-8 rounded-lg bg-indigo-50 dark:bg-indigo-500/15 border border-indigo-200 dark:border-indigo-500/30 flex items-center justify-center text-indigo-600 dark:text-indigo-400">
              <Sparkles className="w-4 h-4" />
            </div>
            <div>
              <h2 className="text-sm font-bold text-slate-900 dark:text-white">新建自定义小组件</h2>
              <p className="text-[11px] text-slate-500 dark:text-slate-400">填写基本信息，快速创建一个可在桌面运行的自定义小组件</p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-1 rounded-lg text-slate-400 hover:text-slate-700 dark:hover:text-white hover:bg-slate-100 dark:hover:bg-slate-800 transition-colors"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Form */}
        <form onSubmit={handleSubmit} className="space-y-4 mt-4 text-xs">
          {/* Template Selection */}
          <div className="space-y-1.5">
            <label className="font-semibold text-slate-700 dark:text-slate-300">选择模板风格</label>
            <div className="grid grid-cols-3 gap-2">
              <button
                type="button"
                onClick={() => setTemplate('vanilla')}
                className={`p-2.5 rounded-xl border text-left flex flex-col gap-1 transition-all ${
                  template === 'vanilla'
                    ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-500/15 text-indigo-700 dark:text-white ring-1 ring-indigo-500/40 shadow-xs'
                    : 'border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-900/60 text-slate-600 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800/80'
                }`}
              >
                <div className={`flex items-center gap-1.5 font-bold text-xs ${template === 'vanilla' ? 'text-indigo-600 dark:text-indigo-400' : 'text-slate-700 dark:text-slate-300'}`}>
                  <Code2 className="w-3.5 h-3.5" />
                  基础网页模板
                </div>
                <span className={`text-[10px] leading-tight ${template === 'vanilla' ? 'text-indigo-600/80 dark:text-indigo-200/70' : 'text-slate-400 dark:text-slate-500'}`}>简单轻量，开箱即用</span>
              </button>

              <button
                type="button"
                onClick={() => setTemplate('react')}
                className={`p-2.5 rounded-xl border text-left flex flex-col gap-1 transition-all ${
                  template === 'react'
                    ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-500/15 text-indigo-700 dark:text-white ring-1 ring-indigo-500/40 shadow-xs'
                    : 'border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-900/60 text-slate-600 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800/80'
                }`}
              >
                <div className={`flex items-center gap-1.5 font-bold text-xs ${template === 'react' ? 'text-indigo-600 dark:text-indigo-400' : 'text-slate-700 dark:text-slate-300'}`}>
                  <Layers className="w-3.5 h-3.5" />
                  现代卡片模板
                </div>
                <span className={`text-[10px] leading-tight ${template === 'react' ? 'text-indigo-600/80 dark:text-indigo-200/70' : 'text-slate-400 dark:text-slate-500'}`}>适合制作多功能交互面板</span>
              </button>

              <button
                type="button"
                onClick={() => setTemplate('webgl')}
                className={`p-2.5 rounded-xl border text-left flex flex-col gap-1 transition-all ${
                  template === 'webgl'
                    ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-500/15 text-indigo-700 dark:text-white ring-1 ring-indigo-500/40 shadow-xs'
                    : 'border-slate-200 dark:border-slate-800 bg-slate-50 dark:bg-slate-900/60 text-slate-600 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-slate-800/80'
                }`}
              >
                <div className={`flex items-center gap-1.5 font-bold text-xs ${template === 'webgl' ? 'text-indigo-600 dark:text-indigo-400' : 'text-slate-700 dark:text-slate-300'}`}>
                  <Cpu className="w-3.5 h-3.5" />
                  动态光影模板
                </div>
                <span className={`text-[10px] leading-tight ${template === 'webgl' ? 'text-indigo-600/80 dark:text-indigo-200/70' : 'text-slate-400 dark:text-slate-500'}`}>适合制作流畅动画特效</span>
              </button>
            </div>
          </div>

          {/* Name & ID */}
          <div className="grid grid-cols-2 gap-3">
            <div className="space-y-1">
              <label className="font-semibold text-slate-700 dark:text-slate-300">组件名称</label>
              <input
                type="text"
                required
                value={name}
                onChange={e => handleNameChange(e.target.value)}
                placeholder="例如: 极简悬浮时钟"
                className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs font-medium placeholder-slate-400 dark:placeholder-slate-500 shadow-2xs"
              />
            </div>
            <div className="space-y-1">
              <label className="font-semibold text-slate-700 dark:text-slate-300">英文代号</label>
              <input
                type="text"
                required
                value={widgetId}
                onChange={e => setWidgetId(e.target.value)}
                placeholder="例如: com.lindesk.clock"
                className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs font-mono placeholder-slate-400 dark:placeholder-slate-500 shadow-2xs"
              />
            </div>
          </div>

          {/* Author & Dimensions */}
          <div className="grid grid-cols-3 gap-3">
            <div className="space-y-1">
              <label className="font-semibold text-slate-700 dark:text-slate-300">作者</label>
              <input
                type="text"
                value={author}
                onChange={e => setAuthor(e.target.value)}
                placeholder="作者名称"
                className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs shadow-2xs"
              />
            </div>
            <div className="space-y-1">
              <label className="font-semibold text-slate-700 dark:text-slate-300">默认宽度 (px)</label>
              <input
                type="number"
                min="100"
                max="3840"
                value={width}
                onChange={e => setWidth(Number(e.target.value))}
                className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs font-mono shadow-2xs"
              />
            </div>
            <div className="space-y-1">
              <label className="font-semibold text-slate-700 dark:text-slate-300">默认高度 (px)</label>
              <input
                type="number"
                min="60"
                max="2160"
                value={height}
                onChange={e => setHeight(Number(e.target.value))}
                className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs font-mono shadow-2xs"
              />
            </div>
          </div>

          {/* Description */}
          <div className="space-y-1">
            <label className="font-semibold text-slate-700 dark:text-slate-300">简介说明</label>
            <textarea
              rows={2}
              value={description}
              onChange={e => setDescription(e.target.value)}
              placeholder="简要描述这个小组件的用途..."
              className="w-full px-3 py-2 rounded-xl bg-slate-50 dark:bg-slate-950/80 border border-slate-200 dark:border-slate-800 focus:border-indigo-500 focus:bg-white dark:focus:bg-slate-950 focus:outline-none text-slate-900 dark:text-slate-100 text-xs placeholder-slate-400 dark:placeholder-slate-500 resize-none shadow-2xs"
            />
          </div>

          {/* Actions */}
          <div className="flex items-center justify-end gap-2.5 pt-3 border-t border-slate-200/80 dark:border-slate-800">
            <button
              type="button"
              onClick={onClose}
              className="px-4 py-2 rounded-xl bg-slate-100 hover:bg-slate-200 dark:bg-slate-800/80 dark:hover:bg-slate-700/80 text-slate-700 dark:text-slate-300 text-xs font-medium transition-colors"
            >
              取消
            </button>
            <button
              type="submit"
              className="px-5 py-2 rounded-xl bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold shadow-md shadow-indigo-600/30 transition-all active:scale-95"
            >
              立即创建
            </button>
          </div>
        </form>
      </div>
    </div>
  );
};
