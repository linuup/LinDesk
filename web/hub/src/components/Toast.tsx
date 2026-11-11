import React from 'react';
import { CheckCircle2, AlertCircle, Info, X } from 'lucide-react';

export interface ToastMessage {
  id: string;
  type: 'success' | 'error' | 'info';
  title: string;
  message?: string;
}

interface ToastProps {
  toasts: ToastMessage[];
  onDismiss: (id: string) => void;
}

export const Toast: React.FC<ToastProps> = ({ toasts, onDismiss }) => {
  if (toasts.length === 0) return null;

  return (
    <div className="fixed bottom-5 right-5 z-50 flex flex-col gap-2 max-w-sm w-full pointer-events-none">
      {toasts.map(toast => {
        const isSuccess = toast.type === 'success';
        const isError = toast.type === 'error';

        return (
          <div
            key={toast.id}
            className={`pointer-events-auto p-3.5 rounded-xl border shadow-xl backdrop-blur-md flex items-start gap-3 transition-all animate-in slide-in-from-bottom-3 duration-200 ${
              isSuccess
                ? 'bg-emerald-50 dark:bg-emerald-950/90 border-emerald-200 dark:border-emerald-500/40 text-emerald-800 dark:text-emerald-200'
                : isError
                ? 'bg-rose-50 dark:bg-rose-950/90 border-rose-200 dark:border-rose-500/40 text-rose-800 dark:text-rose-200'
                : 'bg-white/95 dark:bg-[#131927]/95 border-slate-200 dark:border-slate-700/80 text-slate-800 dark:text-slate-100'
            }`}
          >
            <div className="shrink-0 mt-0.5">
              {isSuccess && <CheckCircle2 className="w-4 h-4 text-emerald-600 dark:text-emerald-400" />}
              {isError && <AlertCircle className="w-4 h-4 text-rose-600 dark:text-rose-400" />}
              {!isSuccess && !isError && <Info className="w-4 h-4 text-indigo-600 dark:text-indigo-400" />}
            </div>

            <div className="flex-1 text-xs">
              <div className="font-bold text-slate-900 dark:text-white">{toast.title}</div>
              {toast.message && (
                <div className="text-[11px] text-slate-600 dark:text-slate-300/80 mt-0.5 leading-relaxed">{toast.message}</div>
              )}
            </div>

            <button
              onClick={() => onDismiss(toast.id)}
              className="text-slate-400 hover:text-slate-700 dark:hover:text-white p-0.5 rounded transition-colors cursor-pointer"
            >
              <X className="w-3.5 h-3.5" />
            </button>
          </div>
        );
      })}
    </div>
  );
};
