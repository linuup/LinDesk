import React, { useState, useEffect, useCallback } from 'react';
import { 
  MainTabType, WidgetInfo, WallpaperInfo, SystemPaths, CreateWidgetData, 
  IPCMessageLog, StagedWallpaperMedia 
} from './types';
import { ipc } from './services/ipc';
import { Header } from './components/Header';
import { Overview } from './components/Overview';
import { WidgetCenter } from './components/WidgetCenter';
import { WallpaperStudio } from './components/WallpaperStudio';
import { WallpaperImportModal } from './components/WallpaperImportModal';
import { DevConsole } from './components/DevConsole';
import { CreateWidgetModal } from './components/CreateWidgetModal';
import { Toast, ToastMessage } from './components/Toast';
import { DevPreviewBanner } from './components/DevPreviewBanner';

export const App: React.FC = () => {
  const [isDebug, setIsDebug] = useState<boolean>(() => {
    if (typeof window !== 'undefined') {
      const params = new URLSearchParams(window.location.search);
      if (params.get('debug') === '1' || params.get('debug') === 'true') return true;
      if (params.get('release') === '1' || params.get('debug') === '0') return false;
    }
    return Boolean(import.meta.env.DEV);
  });

  const [activeTab, setActiveTab] = useState<MainTabType>(() => {
    const saved = localStorage.getItem('lindesk_hub_active_tab') as MainTabType;
    if (saved && ['overview', 'studio', 'widgets'].includes(saved)) {
      return saved;
    }
    if (saved === 'desktop') {
      const isDevEnv = typeof window !== 'undefined' && 
        (new URLSearchParams(window.location.search).get('debug') === '1' || Boolean(import.meta.env.DEV));
      if (isDevEnv) return 'desktop';
    }
    return 'overview';
  });

  useEffect(() => {
    if (!isDebug && activeTab === 'desktop') {
      setActiveTab('overview');
      localStorage.setItem('lindesk_hub_active_tab', 'overview');
    }
  }, [isDebug, activeTab]);

  const [theme, setTheme] = useState<'light' | 'dark'>(() => {
    const saved = localStorage.getItem('lindesk_hub_theme');
    if (saved === 'dark' || saved === 'light') return saved;
    return 'light';
  });

  useEffect(() => {
    document.documentElement.classList.remove('light', 'dark');
    document.documentElement.classList.add(theme);
    localStorage.setItem('lindesk_hub_theme', theme);
  }, [theme]);

  const handleToggleTheme = () => {
    setTheme(prev => (prev === 'dark' ? 'light' : 'dark'));
  };

  const handleTabChange = (tab: MainTabType) => {
    setActiveTab(tab);
    localStorage.setItem('lindesk_hub_active_tab', tab);
  };

  const [selectedStudioWallpaperId, setSelectedStudioWallpaperId] = useState<string>('');

  const [widgets, setWidgets] = useState<WidgetInfo[]>([]);
  const [wallpapers, setWallpapers] = useState<WallpaperInfo[]>([]);
  const [runtimeEnabled, setRuntimeEnabled] = useState(true);
  const [activeWallpaperId, setActiveWallpaperId] = useState('');
  const [systemPaths, setSystemPaths] = useState<SystemPaths | null>(null);
  const [logs, setLogs] = useState<IPCMessageLog[]>(() => ipc.getLogs());
  const [toasts, setToasts] = useState<ToastMessage[]>([]);
  const [createModalOpen, setCreateModalOpen] = useState(false);

  // Staged wallpaper media for import / custom parameter editing modal
  const [stagedMedia, setStagedMedia] = useState<StagedWallpaperMedia | null>(null);
  const [isExternalDragging, setIsExternalDragging] = useState(false);
  const [isMaximized, setIsMaximized] = useState(false);

  useEffect(() => {
    const checkMaximized = () => {
      const isMax = window.innerWidth >= (window.screen.availWidth - 4) && window.innerHeight >= (window.screen.availHeight - 4);
      setIsMaximized(isMax);
    };
    checkMaximized();
    window.addEventListener('resize', checkMaximized);
    return () => window.removeEventListener('resize', checkMaximized);
  }, []);

  const addToast = useCallback((type: 'success' | 'error' | 'info', title: string, message?: string) => {
    const id = `toast-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`;
    setToasts(prev => [...prev, { id, type, title, message }]);
    setTimeout(() => {
      setToasts(prev => prev.filter(t => t.id !== id));
    }, 3500);
  }, []);

  const dismissToast = useCallback((id: string) => {
    setToasts(prev => prev.filter(t => t.id !== id));
  }, []);

  const refreshState = useCallback(() => {
    ipc.send('list');
  }, []);

  useEffect(() => {
    const unsubLog = ipc.onLog(newLog => {
      setLogs(prev => [newLog, ...prev.slice(0, 99)]);
    });

    const unsubMsg = ipc.onMessage((data: any) => {
      if (!data) return;

      const action = data.action || data.type;

      if (action === 'window_state_changed') {
        setIsMaximized(Boolean(data.isMaximized));
      } else if (action === 'drag_state_changed') {
        setIsExternalDragging(Boolean(data.dragging));
      } else if (action === 'stage_wallpaper_media') {
        setIsExternalDragging(false);
        if (data.cancelled) return;
        if (data.ok) {
          setActiveTab('studio');
          localStorage.setItem('lindesk_hub_active_tab', 'studio');
          const ext = (data.filePath || (data.wallpaper && data.wallpaper.sourcePath) || '').split('.').pop() || '';
          const staged: StagedWallpaperMedia = {
            filePath: data.filePath || (data.wallpaper && data.wallpaper.sourcePath) || '',
            fileUrl: data.fileUrl || (data.wallpaper && data.wallpaper.mediaUrl) || '',
            name: data.name || (data.wallpaper && data.wallpaper.name) || '新导入壁纸',
            ext: ext.toUpperCase(),
            wallpaperType: data.wallpaperType || (data.wallpaper && data.wallpaper.wallpaperType) || (data.filePath && /\.(mp4|webm|mov|mkv|avi)$/i.test(data.filePath) ? 'video' : 'image'),
            sizeBytes: data.wallpaper ? data.wallpaper.fileSizeBytes : undefined,
            editWallpaperId: data.id || (data.wallpaper && data.wallpaper.id),
            packagePath: data.wallpaper ? data.wallpaper.packagePath : undefined,
            description: data.wallpaper ? data.wallpaper.description : '',
            isArchived: data.wallpaper ? data.wallpaper.isArchived : false,
            customConfig: data.wallpaper ? data.wallpaper.customConfig : undefined,
            isNewlyStaged: true
          };
          setStagedMedia(staged);
          refreshState();
        } else {
          addToast('error', '无法导入该文件', data.error || '请拖入 MP4/WebM 视频或 PNG/JPG/WebP/GIF 图片');
        }
      } else if (action === 'archive_wallpaper_media') {
        if (data.ok) {
          addToast('success', '视频已成功归档到壁纸库', data.name ? ('壁纸：' + data.name) : undefined);
          refreshState();
        } else {
          addToast('error', '归档失败', data.error || '无法复制文件');
        }
      } else if (action === 'relocate_wallpaper_media') {
        if (data.cancelled) return;
        if (data.ok) {
          addToast('success', '壁纸源文件已成功重新定位', data.name ? ('壁纸：' + data.name) : undefined);
          refreshState();
        } else {
          addToast('error', '重新定位失败', data.error || '无法定位该文件');
        }
      } else if (action === 'confirm_wallpaper_import' || action === 'update_wallpaper_config') {
        if (data.ok) {
          setStagedMedia(null);
          if (typeof data.activeWallpaperId === 'string' && data.activeWallpaperId) {
            setActiveWallpaperId(data.activeWallpaperId);
          }
          addToast(
            'success',
            data.applied ? '壁纸已保存并设为当前桌面壁纸' : '壁纸已保存到壁纸库',
            data.name ? `壁纸名称：${data.name}` : undefined
          );
          refreshState();
        } else {
          addToast('error', '壁纸保存失败', data.error || '未知错误');
        }
      } else if (action === 'delete_wallpaper') {
        if (data.ok) {
          if (typeof data.activeWallpaperId === 'string') {
            setActiveWallpaperId(data.activeWallpaperId);
          }
          addToast('info', '已从壁纸库中移除该壁纸');
          refreshState();
        } else {
          addToast('error', '删除壁纸失败', data.error || '无法删除该目录');
        }
      } else if (action === 'runtime_state_changed') {
        if (typeof data.runtimeEnabled === 'boolean') {
          setRuntimeEnabled(data.runtimeEnabled);
        }
        refreshState();
      } else if (action === 'set_master_enabled') {
        if (typeof data.enabled === 'boolean') {
          setRuntimeEnabled(data.enabled);
          addToast('info', data.enabled ? '已开启桌面小组件' : '已关闭桌面小组件');
        }
        refreshState();
      } else if (action === 'list') {
        if (typeof data.isDebug === 'boolean') {
          setIsDebug(data.isDebug);
        }
        if (Array.isArray(data.widgets)) {
          setWidgets(data.widgets.filter((w: WidgetInfo) => w.role !== 'wallpaper'));
        }
        if (Array.isArray(data.wallpapers)) {
          setWallpapers(data.wallpapers);
        }
        if (typeof data.runtimeEnabled === 'boolean') {
          setRuntimeEnabled(data.runtimeEnabled);
        }
        if (typeof data.activeWallpaperId === 'string') {
          setActiveWallpaperId(data.activeWallpaperId);
        }
        if (data.userWidgetsDir || data.logDir) {
          setSystemPaths({
            userWidgetsDir: data.userWidgetsDir || '',
            userLogsDir: data.logDir || ''
          });
        }
      } else if (action === 'launch' || action === 'start') {
        if (data.ok) {
          addToast('success', '组件启动成功');
          refreshState();
        } else {
          addToast('error', '启动失败', data.error || '未知错误');
        }
      } else if (action === 'stop' || action === 'unload') {
        if (data.ok) {
          addToast('info', '组件已停止');
          refreshState();
        }
      } else if (action === 'restart') {
        if (data.ok) {
          addToast('success', '组件已重新加载');
          refreshState();
        }
      } else if (action === 'apply_wallpaper') {
        if (data.ok) {
          if (typeof data.activeWallpaperId === 'string' && data.activeWallpaperId) {
            setActiveWallpaperId(data.activeWallpaperId);
          }
          addToast('success', '壁纸已应用至桌面');
          refreshState();
        } else {
          addToast('error', '壁纸启用失败', data.error || '未知错误');
        }
      } else if (action === 'stop_wallpaper') {
        if (data.ok) {
          addToast('info', '动态壁纸已关闭');
          setActiveWallpaperId('');
          refreshState();
        }
      } else if (action === 'save_widget_settings') {
        if (data.ok) {
          addToast('success', '设置已保存');
          refreshState();
        }
      } else if (action === 'create_project') {
        if (data.ok) {
          addToast('success', '新组件创建成功');
          setCreateModalOpen(false);
          refreshState();
        } else {
          addToast('error', '创建失败', data.error || '未知异常');
        }
      } else if (action === 'uninstall') {
        if (data.ok) {
          addToast('info', '组件已成功删除');
          refreshState();
        } else {
          addToast('error', '删除失败', data.error || '无法删除组件目录');
        }
      } else if (action === 'import_wallpaper_media') {
        if (data.cancelled) return;
        if (data.ok) {
          if (typeof data.activeWallpaperId === 'string' && data.activeWallpaperId) {
            setActiveWallpaperId(data.activeWallpaperId);
          } else if (typeof data.id === 'string' && data.id) {
            setActiveWallpaperId(data.id);
          }
          addToast('success', '壁纸已导入并应用', data.name ? `当前壁纸：${data.name}` : undefined);
          refreshState();
        } else {
          addToast('error', '导入失败', data.error || '不支持的文件格式');
        }
      } else if (action === 'import_dev_project') {
        if (data.cancelled) return;
        if (data.ok) {
          addToast('success', '本地组件已导入并启动', data.name ? `已加载：${data.name}` : undefined);
          refreshState();
        } else {
          addToast('error', '组件导入失败', data.error || '请选择包含有效组件文件的文件夹');
        }
      }
    });

    refreshState();

    return () => {
      unsubLog();
      unsubMsg();
    };
  }, [addToast, refreshState]);

  // 实时检测：窗口激活/切换回前台时自动刷新壁纸资源状态
  useEffect(() => {
    const handleFocus = () => {
      refreshState();
    };
    const handleVisibility = () => {
      if (document.visibilityState === 'visible') {
        refreshState();
      }
    };
    window.addEventListener('focus', handleFocus);
    document.addEventListener('visibilitychange', handleVisibility);
    return () => {
      window.removeEventListener('focus', handleFocus);
      document.removeEventListener('visibilitychange', handleVisibility);
    };
  }, [refreshState]);

  // 实时检测：当画廊中存在丢失文件时，轻量级心跳持续探测其是否已放回原处
  useEffect(() => {
    const hasMissing = wallpapers.some(w => w.isMissing);
    if (!hasMissing) return;

    const timer = setInterval(() => {
      refreshState();
    }, 2000);

    return () => clearInterval(timer);
  }, [wallpapers, refreshState]);

  const handleToggleMaster = (enabled: boolean) => {
    setRuntimeEnabled(enabled);
    ipc.send('set_master_enabled', { enabled });
  };

  const handleLaunch = (id: string, packagePath: string) => {
    ipc.send('launch', { widgetId: id, packagePath });
  };

  const handleStop = (id: string) => {
    ipc.send('stop', { widgetId: id });
  };

  const handleRestart = (id: string) => {
    ipc.send('restart', { widgetId: id });
  };

  const handleTogglePermission = (id: string, perm: string, granted: boolean) => {
    ipc.send('set_permission', { widgetId: id, permission: perm, granted });
  };

  const handleClearStorage = (id: string) => {
    ipc.send('clear_storage', { widgetId: id });
  };

  const handleOpenFolder = (_id: string, path: string) => {
    ipc.send('open_folder', { path });
  };

  const handleOpenWidgetsDir = () => {
    ipc.send('open_widgets_folder');
  };

  const handleOpenLogsDir = () => {
    ipc.send('open_logs_folder');
  };

  const handleShowDevTools = (id: string) => {
    ipc.send('show_devtools', { widgetId: id });
  };

  const handleUninstall = (id: string, packagePath?: string, name?: string) => {
    ipc.send('uninstall', { widgetId: id, packagePath, name });
  };

  const handleCreateWidget = (data: CreateWidgetData) => {
    ipc.send('create_project', data);
  };

  const handleImportDevProject = () => {
    ipc.send('import_dev_project');
  };

  const handleSaveWidgetSettings = (widgetId: string, settings: Record<string, any>) => {
    ipc.send('save_widget_settings', { widgetId, settings });
    addToast('success', '组件配置已保存');
    refreshState();
  };

  const handleApplyWallpaper = (id: string, packagePath?: string) => {
    setActiveWallpaperId(id);
    ipc.send('apply_wallpaper', { widgetId: id, packagePath });
  };

  const handleStopWallpaper = () => {
    setActiveWallpaperId('');
    ipc.send('stop_wallpaper');
  };

  const handleSendProperty = (property: string, value: any, widgetId?: string, packagePath?: string) => {
    ipc.send('set_wallpaper_property', { property, value, widgetId, packagePath });
    setWallpapers(prev =>
      prev.map(w => {
        if (!widgetId || w.id !== widgetId) return w;
        if (property === 'interactive') {
          return { ...w, interactive: Boolean(value) };
        }
        const nextCustomConfig = { ...(w.customConfig || {}), [property]: value };
        const nextProperties = w.properties ? { ...w.properties } : undefined;
        if (nextProperties && nextProperties[property] && typeof nextProperties[property] === 'object') {
          nextProperties[property] = { ...nextProperties[property], value };
        }
        return {
          ...w,
          customConfig: nextCustomConfig,
          properties: nextProperties
        };
      })
    );
  };

  const handleImportMedia = (pathOrName?: string) => {
    if (pathOrName && (pathOrName.includes('\\') || pathOrName.includes('/'))) {
      ipc.send('stage_wallpaper_media', { filePath: pathOrName });
    } else {
      ipc.send('import_wallpaper_media');
    }
  };

  const handleArchiveWallpaper = (id: string) => {
    ipc.send('archive_wallpaper_media', { id });
  };

  const handleRelocateWallpaper = (id: string, filePath?: string) => {
    ipc.send('relocate_wallpaper_media', { id, filePath });
  };

  const handleCancelStagedMedia = useCallback(() => {
    if (stagedMedia?.isNewlyStaged && stagedMedia.editWallpaperId) {
      ipc.send('delete_wallpaper', {
        widgetId: stagedMedia.editWallpaperId,
        id: stagedMedia.editWallpaperId,
        packagePath: stagedMedia.packagePath || stagedMedia.filePath
      });
      addToast('info', '已取消导入壁纸');
    }
    setStagedMedia(null);
  }, [stagedMedia, addToast]);

  const handleConfirmWallpaperImport = (payload: {
    filePath: string;
    name: string;
    description: string;
    scaling: 'cover' | 'contain' | 'fill';
    dimming: number;
    blur: number;
    volume: number;
    muted: boolean;
    playbackRate: number;
    audioDucking: boolean;
    applyImmediately: boolean;
    editWallpaperId?: string;
    packagePath?: string;
  }) => {
    ipc.send('confirm_wallpaper_import', payload);
    if (payload.applyImmediately && payload.editWallpaperId) {
      ipc.send('apply_wallpaper', {
        widgetId: payload.editWallpaperId,
        id: payload.editWallpaperId,
        packagePath: payload.packagePath
      });
    }
    setStagedMedia(null);
  };

  const handleDeleteWallpaper = (id: string, packagePath: string, _name: string) => {
    ipc.send('delete_wallpaper', { widgetId: id, packagePath });
  };

  const handleGoToStudio = (wallpaperId?: string) => {
    if (wallpaperId) {
      setSelectedStudioWallpaperId(wallpaperId);
    }
    handleTabChange('studio');
  };

  const handleGoToWidgets = () => {
    handleTabChange('widgets');
  };

  return (
    <div className="min-h-screen bg-[#f1f3f6] dark:bg-[#0b0f19] text-slate-800 dark:text-slate-100 flex flex-col font-sans select-none antialiased transition-colors">
      {/* Windows 10 标准 1px 窗体描边 (最大化时隐藏，彻底解决白色窗体混淆问题) */}
      {!isMaximized && (
        <div className="fixed inset-0 pointer-events-none z-[9999] border border-[#707070] dark:border-slate-700" />
      )}

      {/* 1. Full-width Custom Frameless Title Bar */}
      <Header
        activeTab={activeTab}
        onTabChange={handleTabChange}
        totalWidgets={widgets.length}
        theme={theme}
        onToggleTheme={handleToggleTheme}
        isDebug={isDebug}
      />

      {/* 2. Workspace View */}
      <div className="p-4 md:p-6 max-w-7xl w-full mx-auto flex-1 flex flex-col">
        {/* Tab Views */}
        <main className="flex-1">
          {activeTab === 'overview' && (
            <Overview
              wallpapers={wallpapers}
              widgets={widgets}
              activeWallpaperId={activeWallpaperId}
              onApplyWallpaper={handleApplyWallpaper}
              onStopWallpaper={handleStopWallpaper}
              onGoToStudio={handleGoToStudio}
              onGoToWidgets={handleGoToWidgets}
              onImportMedia={() => handleImportMedia()}
              onLaunchWidget={handleLaunch}
              onStopWidget={handleStop}
              onCreateWidget={() => setCreateModalOpen(true)}
            />
          )}

          {activeTab === 'studio' && (
            <WallpaperStudio
              wallpapers={wallpapers}
              activeWallpaperId={activeWallpaperId}
              isExternalDragging={isExternalDragging}
              onApplyWallpaper={handleApplyWallpaper}
              onStopWallpaper={handleStopWallpaper}
              onSendProperty={handleSendProperty}
              onImportMedia={handleImportMedia}
              onArchiveWallpaper={handleArchiveWallpaper}
              onRelocateWallpaper={handleRelocateWallpaper}
              onEditCustomWallpaper={(media) => setStagedMedia(media)}
              onDeleteWallpaper={handleDeleteWallpaper}
              onOpenFolder={handleOpenFolder}
              selectedWallpaperId={selectedStudioWallpaperId}
            />
          )}

          {activeTab === 'widgets' && (
            <WidgetCenter
              widgets={widgets}
              runtimeEnabled={runtimeEnabled}
              onToggleMaster={handleToggleMaster}
              onLaunch={handleLaunch}
              onStop={handleStop}
              onRestart={handleRestart}
              onTogglePermission={handleTogglePermission}
              onClearStorage={handleClearStorage}
              onOpenFolder={handleOpenFolder}
              onOpenWidgetsDir={handleOpenWidgetsDir}
              onShowDevTools={handleShowDevTools}
              onUninstall={handleUninstall}
              onSaveSettings={handleSaveWidgetSettings}
              onOpenCreateModal={() => setCreateModalOpen(true)}
              onImportDevProject={handleImportDevProject}
            />
          )}

          {activeTab === 'desktop' && isDebug && (
            <DevConsole
              logs={logs}
              systemPaths={systemPaths}
              onClearLogs={() => setLogs([])}
              onOpenLogsFolder={handleOpenLogsDir}
              onOpenWidgetsFolder={handleOpenWidgetsDir}
            />
          )}
        </main>
      </div>

      <WallpaperImportModal
        media={stagedMedia}
        onClose={handleCancelStagedMedia}
        onConfirm={handleConfirmWallpaperImport}
      />

      <CreateWidgetModal
        isOpen={createModalOpen}
        onClose={() => setCreateModalOpen(false)}
        onCreate={handleCreateWidget}
      />

      <Toast toasts={toasts} onDismiss={dismissToast} />
      <DevPreviewBanner />
    </div>
  );
};

export default App;
