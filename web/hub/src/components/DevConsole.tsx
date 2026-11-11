import React from 'react';
import { Terminal, Trash2, FolderOpen, ShieldAlert, Cpu } from 'lucide-react';
import { IPCMessageLog, SystemPaths } from '../types';

interface DevConsoleProps {
  logs: IPCMessageLog[];
  systemPaths: SystemPaths | null;
  onClearLogs: () => void;
  onOpenLogsFolder: () => void;
  onOpenWidgetsFolder: () => void;
}

export const DevConsole: React.FC<DevConsoleProps> = ({
  logs,
  systemPaths,
  onClearLogs,
  onOpenLogsFolder,
  onOpenWidgetsFolder
}) => {
  return (
    <div className="space-y-5 animate-in fade-in duration-150 pb-8">
      {/* 1. System Paths & Runtime Environment */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Left: System Paths */}
        <div className="rounded-2xl border border-slate-200/90 dark:border-slate-800/90 bg-white dark:bg-[#131927]/90 p-5 shadow-xs dark:shadow-md space-y-3.5">
          <div className="flex items-center justify-between border-b border-slate-200/80 dark:border-slate-800/80 pb-2.5">
            <h3 className="text-xs font-bold text-slate-900 dark:text-slate-100 flex items-center gap-2 tracking-wider">
              <Cpu className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
              文件存储位置
            </h3>
            <span className="text-[10px] font-medium text-indigo-700 dark:text-indigo-300 bg-indigo-50 dark:bg-indigo-950/80 border border-indigo-200 dark:border-indigo-800/60 px-2 py-0.5 rounded-full">
              本地文件夹
            </span>
          </div>

          <div className="space-y-2 text-xs">
            <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 space-y-1 shadow-2xs">
              <div className="flex items-center justify-between">
                <span className="text-[11px] font-medium text-slate-500 dark:text-slate-400">壁纸与小组件存储文件夹</span>
                <button
                  onClick={onOpenWidgetsFolder}
                  className="text-indigo-600 hover:text-indigo-500 dark:text-indigo-400 dark:hover:text-indigo-300 font-semibold text-[11px] flex items-center gap-1 cursor-pointer"
                >
                  <FolderOpen className="w-3 h-3" />
                  打开文件夹
                </button>
              </div>
              <div className="font-mono text-[11px] text-slate-700 dark:text-slate-200 break-all select-all font-medium pt-0.5">
                {systemPaths?.userWidgetsDir || '%APPDATA%\\LinDesk\\LinWidgets'}
              </div>
            </div>

            <div className="p-3 rounded-xl bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 space-y-1 shadow-2xs">
              <div className="flex items-center justify-between">
                <span className="text-[11px] font-medium text-slate-500 dark:text-slate-400">运行日志文件夹</span>
                <button
                  onClick={onOpenLogsFolder}
                  className="text-emerald-600 hover:text-emerald-500 dark:text-emerald-400 dark:hover:text-emerald-300 font-semibold text-[11px] flex items-center gap-1 cursor-pointer"
                >
                  <FolderOpen className="w-3 h-3" />
                  打开文件夹
                </button>
              </div>
              <div className="font-mono text-[11px] text-slate-700 dark:text-slate-200 break-all select-all font-medium pt-0.5">
                {systemPaths?.userLogsDir || '%APPDATA%\\LinDesk\\logs'}
              </div>
            </div>
          </div>
        </div>

        {/* Right: Security & Architecture */}
        <div className="rounded-2xl border border-slate-200/90 dark:border-slate-800/90 bg-white dark:bg-[#131927]/90 p-5 shadow-xs dark:shadow-md space-y-3.5">
          <div className="flex items-center justify-between border-b border-slate-200/80 dark:border-slate-800/80 pb-2.5">
            <h3 className="text-xs font-bold text-slate-900 dark:text-slate-100 flex items-center gap-2 tracking-wider">
              <ShieldAlert className="w-3.5 h-3.5 text-indigo-600 dark:text-indigo-400" />
              运行状态与保护
            </h3>
            <span className="text-[10px] font-medium text-emerald-700 dark:text-emerald-300 bg-emerald-50 dark:bg-emerald-950/80 border border-emerald-200 dark:border-emerald-800/60 px-2 py-0.5 rounded-full">
              运行正常
            </span>
          </div>

          <div className="space-y-2 text-xs">
            <div className="flex items-center justify-between p-2.5 rounded-xl bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shadow-2xs">
              <div>
                <div className="text-slate-800 dark:text-slate-200 font-medium">桌面图标正常点击</div>
                <div className="text-[10px] text-slate-500 dark:text-slate-400">动态壁纸位于桌面图标下方，不遮挡日常操作</div>
              </div>
              <span className="text-[11px] text-emerald-700 dark:text-emerald-300 bg-emerald-50 dark:bg-emerald-950/80 border border-emerald-200 dark:border-emerald-800/60 px-2 py-0.5 rounded font-semibold">
                正常
              </span>
            </div>

            <div className="flex items-center justify-between p-2.5 rounded-xl bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shadow-2xs">
              <div>
                <div className="text-slate-800 dark:text-slate-200 font-medium">本地文件安全保护</div>
                <div className="text-[10px] text-slate-500 dark:text-slate-400">防止组件随意访问无关系统文件</div>
              </div>
              <span className="text-[11px] text-indigo-700 dark:text-indigo-300 bg-indigo-50 dark:bg-indigo-950/80 border border-indigo-200 dark:border-indigo-800/60 px-2 py-0.5 rounded font-semibold">
                已开启
              </span>
            </div>

            <div className="flex items-center justify-between p-2.5 rounded-xl bg-slate-50 dark:bg-slate-900 border border-slate-200 dark:border-slate-800 shadow-2xs">
              <div>
                <div className="text-slate-800 dark:text-slate-200 font-medium">防卡死独立运行</div>
                <div className="text-[10px] text-slate-500 dark:text-slate-400">单个壁纸或小组件异常时不会影响桌面主程序</div>
              </div>
              <span className="text-[11px] text-purple-700 dark:text-purple-300 bg-purple-50 dark:bg-purple-950/80 border border-purple-200 dark:border-purple-800/60 px-2 py-0.5 rounded font-semibold">
                已开启
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* 2. Live IPC Event Bus Monitor */}
      <div className="rounded-2xl border border-slate-200/90 dark:border-slate-800/90 bg-white dark:bg-[#131927]/90 p-5 shadow-xs dark:shadow-md space-y-3.5">
        <div className="flex items-center justify-between border-b border-slate-200/80 dark:border-slate-800/80 pb-2.5">
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />
            <h3 className="text-xs font-bold text-slate-900 dark:text-slate-100 tracking-wider">
              操作与运行记录
            </h3>
            <span className="text-[10px] font-mono text-slate-400 dark:text-slate-500">
              ({logs.length} 条记录)
            </span>
          </div>
          <button
            onClick={onClearLogs}
            className="px-2.5 py-1 rounded-lg text-slate-500 hover:text-slate-900 dark:text-slate-400 dark:hover:text-slate-100 hover:bg-slate-100 dark:hover:bg-slate-800 text-xs flex items-center gap-1 transition-colors cursor-pointer"
          >
            <Trash2 className="w-3.5 h-3.5" />
            清空记录
          </button>
        </div>

        {/* Scrollable Log Terminal */}
        <div className="h-80 rounded-xl bg-slate-950 border border-slate-800 p-3.5 font-mono text-[11px] overflow-y-auto space-y-1.5 scrollbar-thin text-slate-200 shadow-inner">
          {logs.length === 0 ? (
            <div className="text-slate-500 text-center py-16">暂无运行记录</div>
          ) : (
            logs.map(log => (
              <div key={log.id} className="flex items-start gap-2 leading-relaxed">
                <span className="text-slate-500 shrink-0 font-mono text-[10px]">
                  {log.timestamp}
                </span>
                <span
                  className={`px-1.5 py-0.2 rounded text-[10px] font-bold shrink-0 ${
                    log.direction === 'in'
                      ? 'bg-blue-500/20 text-blue-400'
                      : log.direction === 'out'
                      ? 'bg-emerald-500/20 text-emerald-400'
                      : 'bg-purple-500/20 text-purple-400'
                  }`}
                >
                  {log.direction.toUpperCase()}
                </span>
                <span className="text-indigo-400 font-semibold shrink-0">{log.action}:</span>
                <span className="text-slate-300 break-all">
                  {typeof log.payload === 'object' ? JSON.stringify(log.payload) : String(log.payload)}
                </span>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
};
