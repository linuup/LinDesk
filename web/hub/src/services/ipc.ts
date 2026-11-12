import { ManagerState, WidgetInfo, IPCMessageLog } from '../types';

class IPCService {
  private isCEF: boolean;
  private messageListeners: ((data: any) => void)[] = [];
  private logListeners: ((log: IPCMessageLog) => void)[] = [];
  private messageLogs: IPCMessageLog[] = [];

  // Browser Mock Data Store (reflects the exact 7 real desk widgets and 3 real wallpapers on disk)
  private mockState: ManagerState = {
    runtimeEnabled: true,
    showBounds: false,
    activeWallpaperId: 'lindesk.wallpaper',
    activeWallpaperPackagePath: 'resources/wallpapers/living_wallpaper',
    activeWallpaperInteractive: true,
    userWidgetsDir: 'C:\\Users\\User\\AppData\\Roaming\\LinDesk\\LinWidgets',
    logDir: 'C:\\Users\\User\\AppData\\Local\\LinDesk\\logs',
    widgets: [
      {
        id: 'lindesk.clock',
        name: '数字极简时钟 (Clock)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '拟态透明数字时钟，支持 12/24 小时制与时钟秒针平滑流式渲染。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/Clock',
        source: '内置组件',
        isDevelopment: false,
        running: true,
        status: 'running',
        crashCount: 0,
        storageUsedBytes: 24576,
        declaredPermissions: [],
        grantedPermissions: [],
        hasSettings: true,
        settingsSchema: [
          {
            key: 'clockTheme',
            type: 'select',
            title: '时钟外观主题',
            description: '选择纯净深色或通透浅色外观',
            default: 'light',
            options: [
              { label: '极简明亮 (Light)', value: 'light' },
              { label: '暗夜深邃 (Dark)', value: 'dark' }
            ]
          },
          {
            key: 'clockStyle',
            type: 'select',
            title: '表盘时间风格',
            description: '切换数字时钟、经典表盘或极简专注',
            default: 'digital',
            options: [
              { label: '现代数字时钟', value: 'digital' },
              { label: '经典模拟表盘', value: 'analog' },
              { label: '极简专注模式', value: 'focus' }
            ]
          },
          {
            key: 'showSeconds',
            type: 'boolean',
            title: '秒针动态流式渲染',
            description: '是否在表盘实时显示流式秒针与秒数读数',
            default: true
          }
        ],
        settingsValues: {
          clockTheme: 'light',
          clockStyle: 'digital',
          showSeconds: true
        },
        runningInstanceIds: ['installed:lindesk.clock'],
        instances: [{
          instanceId: 'installed:lindesk.clock',
          running: true,
          visible: true,
          geometry: { x: 531, y: 110, width: 201, height: 201 }
        }]
      },
      {
        id: 'lindesk.sysmonitor',
        name: '系统性能监控 (SysMonitor)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '高精采样 CPU、显存、网络吞吐与内存负载，紧凑状态面板。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/SysMonitor',
        source: '内置组件',
        isDevelopment: false,
        running: false,
        status: 'stopped',
        crashCount: 0,
        storageUsedBytes: 40960,
        declaredPermissions: ['systemInfo', 'storage'],
        grantedPermissions: ['systemInfo', 'storage'],
        hasSettings: true,
        runningInstanceIds: []
      },
      {
        id: 'lindesk.todo',
        name: '桌面待办清单 (Todo)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '轻量化桌面备忘待办小清单，支持快速勾选完成与条目归档。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/Todo',
        source: '内置组件',
        isDevelopment: false,
        running: true,
        status: 'running',
        crashCount: 0,
        storageUsedBytes: 16384,
        declaredPermissions: ['storage'],
        grantedPermissions: ['storage'],
        hasSettings: true,
        settingsFile: 'settings.html',
        runningInstanceIds: ['installed:lindesk.todo']
      },
      {
        id: 'lindesk.weather',
        name: '实时天气浮窗 (Weather)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '基于地理位置实时查询温度、气象变化及空气质量。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/Weather',
        source: '内置组件',
        isDevelopment: false,
        running: false,
        status: 'stopped',
        crashCount: 0,
        storageUsedBytes: 32768,
        declaredPermissions: ['network', 'storage'],
        grantedPermissions: ['network', 'storage'],
        hasSettings: true,
        runningInstanceIds: []
      },
      {
        id: 'lindesk.watertracker.lin',
        name: '健康喝水提醒 (Water Tracker)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '日常饮水量追踪与定时提醒，帮助保持健康体征。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/WaterTracker',
        source: '内置组件',
        isDevelopment: false,
        running: true,
        status: 'running',
        crashCount: 0,
        storageUsedBytes: 8192,
        declaredPermissions: ['storage'],
        grantedPermissions: ['storage'],
        hasSettings: false,
        runningInstanceIds: ['installed:lindesk.watertracker.lin']
      },
      {
        id: 'lindesk.notes.lin',
        name: '桌面便签便条 (Notes)',
        version: '1.0.0',
        author: 'LinDesk',
        description: '便签便利贴，支持随时记录灵感碎片，支持富文本排版。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/Notes',
        source: '内置组件',
        isDevelopment: false,
        running: false,
        status: 'stopped',
        crashCount: 0,
        storageUsedBytes: 12288,
        declaredPermissions: ['storage'],
        grantedPermissions: ['storage'],
        hasSettings: true,
        settingsFile: 'settings.html',
        runningInstanceIds: []
      },
      {
        id: 'lindesk.timer',
        name: '倒计时番茄钟 (Timer)',
        version: '1.0.0',
        author: 'LinDesk',
        description: 'Fluent 设计风格倒计时器与番茄工作法时间管理器。',
        role: 'widget',
        packagePath: 'resources/LinWidgets/Timer',
        source: '内置组件',
        isDevelopment: false,
        running: false,
        status: 'stopped',
        crashCount: 0,
        storageUsedBytes: 4096,
        declaredPermissions: [],
        grantedPermissions: [],
        hasSettings: false,
        runningInstanceIds: []
      }
    ],
    wallpapers: [
      {
        id: 'lindesk.wallpaper',
        name: 'LinDesk Living Wallpaper 流体极光',
        version: '1.1.0',
        author: 'LinDesk',
        description: '官方交互式动态壁纸：流体极光、宇宙引力奇点与量子网格，支持桌面鼠标物理波动。',
        packagePath: 'resources/wallpapers/living_wallpaper',
        role: 'wallpaper',
        interactive: true,
        running: true,
        tag: '流体动力学'
      },
      {
        id: 'com.lindesk.wallpaper.neon_universe',
        name: 'Breeze Meadow 清风原野',
        version: '2.0.0',
        author: 'LinDesk',
        description: '纯代码手绘治愈系自然动态壁纸：晨曦天青、柔风草浪、丁达尔晨光与起舞蒲公英，随鼠标拂动荡漾，极度养眼护眼。',
        packagePath: 'resources/wallpapers/neon_universe',
        role: 'wallpaper',
        interactive: true,
        running: false,
        tag: '清新自然',
        properties: {
          windSpeed: {
            type: 'slider',
            text: '微风律动速率 (Wind Speed)',
            min: 1,
            max: 10,
            step: 1,
            value: 4
          },
          dandelionCount: {
            type: 'slider',
            text: '蒲公英飞絮密度 (Dandelions)',
            min: 10,
            max: 80,
            step: 5,
            value: 35
          },
          sunbeamIntensity: {
            type: 'slider',
            text: '晨曦光束明度 (Sunbeams)',
            min: 0,
            max: 10,
            step: 1,
            value: 6
          },
          showClock: {
            type: 'boolean',
            text: '显示极简环境时钟 (Clock)',
            value: true
          },
          clockPosX: {
            type: 'slider',
            text: '时钟水平位置 X% (Clock X)',
            min: 0,
            max: 100,
            step: 1,
            value: 50
          },
          clockPosY: {
            type: 'slider',
            text: '时钟垂直位置 Y% (Clock Y)',
            min: 0,
            max: 100,
            step: 1,
            value: 50
          },
          clockScale: {
            type: 'slider',
            text: '时钟大小比例 % (Clock Size)',
            min: 60,
            max: 160,
            step: 5,
            value: 100
          },
          clockBgOpacity: {
            type: 'slider',
            text: '时钟卡片磨砂透明度 % (Card Opacity)',
            min: 0,
            max: 100,
            step: 5,
            value: 45
          },
          clockColor: {
            type: 'color',
            text: '时钟文字颜色 (Text Color)',
            value: '#1e3a2b'
          },
          showDate: {
            type: 'boolean',
            text: '显示日期与微标 (Show Date & Badge)',
            value: true
          }
        }
      }
    ]
  };

  private originalWidgets: WidgetInfo[] = [];
  private originalWallpapers: any[] = [];
  private mockMode: 'rich' | 'empty' | 'wallpapers_only' | 'widgets_only' = 'rich';

  constructor() {
    this.originalWidgets = JSON.parse(JSON.stringify(this.mockState.widgets));
    this.originalWallpapers = JSON.parse(JSON.stringify(this.mockState.wallpapers));

    this.isCEF = typeof window !== 'undefined' && 
      (window.location.protocol === 'file:' || 
       window.location.protocol === 'linwidget:' || 
       window.location.search.includes('lindesk=1') ||
       window.location.search.includes('cef=1') ||
       (window as any).__linwidget_bridge_injected === true ||
       window.navigator.userAgent.includes('LinDesk'));

    if (typeof window !== 'undefined') {
      window.addEventListener('message', (event) => {
        let data = event.data;
        if (typeof data === 'string') {
          try { data = JSON.parse(data); } catch { return; }
        }
        if (data && typeof data === 'object') {
          this.recordLog('in', data.action || data.type || 'message', data, data.ok);
          this.notifyListeners(data);
        }
      });
    }
  }

  public getMockMode(): 'rich' | 'empty' | 'wallpapers_only' | 'widgets_only' {
    return this.mockMode;
  }

  public setMockMode(mode: 'rich' | 'empty' | 'wallpapers_only' | 'widgets_only') {
    this.mockMode = mode;
    if (mode === 'rich') {
      this.mockState.widgets = JSON.parse(JSON.stringify(this.originalWidgets));
      this.mockState.wallpapers = JSON.parse(JSON.stringify(this.originalWallpapers));
      this.mockState.activeWallpaperId = 'lindesk.wallpaper';
    } else if (mode === 'empty') {
      this.mockState.widgets = [];
      this.mockState.wallpapers = [];
      this.mockState.activeWallpaperId = '';
    } else if (mode === 'wallpapers_only') {
      this.mockState.widgets = [];
      this.mockState.wallpapers = JSON.parse(JSON.stringify(this.originalWallpapers));
      this.mockState.activeWallpaperId = 'lindesk.wallpaper';
    } else if (mode === 'widgets_only') {
      this.mockState.widgets = JSON.parse(JSON.stringify(this.originalWidgets));
      this.mockState.wallpapers = [];
      this.mockState.activeWallpaperId = '';
    }
    this.send('list');
  }

  public simulateDragWallpaper(name: string = '示例动态壁纸', fileUrl: string = 'https://assets.mixkit.co/videos/preview/mixkit-tree-branches-in-the-breeze-1188-large.mp4') {
    this.notifyListeners({
      action: 'stage_wallpaper_media',
      ok: true,
      name,
      fileUrl,
      filePath: 'C:\\Users\\User\\Videos\\sample_wallpaper.mp4',
      wallpaper: {
        id: `mock.custom.${Date.now()}`,
        name,
        mediaUrl: fileUrl,
        sourcePath: 'C:\\Users\\User\\Videos\\sample_wallpaper.mp4'
      }
    });
  }

  public getIsCEF(): boolean {
    return this.isCEF;
  }

  public getLogs(): IPCMessageLog[] {
    return this.messageLogs;
  }

  public onMessage(callback: (data: any) => void) {
    this.messageListeners.push(callback);
    return () => {
      this.messageListeners = this.messageListeners.filter(cb => cb !== callback);
    };
  }

  public onLog(callback: (log: IPCMessageLog) => void) {
    this.logListeners.push(callback);
    return () => {
      this.logListeners = this.logListeners.filter(cb => cb !== callback);
    };
  }

  private notifyListeners(data: any) {
    this.messageListeners.forEach(cb => {
      try { cb(data); } catch (e) { console.error('IPC dispatch error:', e); }
    });
  }

  private recordLog(direction: 'in' | 'out', action: string, payload: any, ok?: boolean) {
    const log: IPCMessageLog = {
      id: `log-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`,
      timestamp: new Date().toLocaleTimeString(),
      direction,
      action,
      payload,
      ok: ok !== undefined ? ok : true
    };
    this.messageLogs.unshift(log);
    if (this.messageLogs.length > 100) {
      this.messageLogs.pop();
    }
    this.logListeners.forEach(cb => cb(log));
  }

  public send(action: string, payload: Record<string, any> = {}) {
    const reqId = `req-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`;
    const msg = {
      type: 'widget_manager_request',
      action,
      widgetId: payload.widgetId || 'lindesk.hub',
      requestId: reqId,
      ...payload
    };

    this.recordLog('out', action, msg);

    if (this.isCEF) {
      try {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', 'linwidget://bridge/postMessage', true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(JSON.stringify(msg));
      } catch (err) {
        console.warn('[IPC] CEF bridge postMessage error, falling back to mock:', err);
        this.handleMockResponse(action, payload, reqId);
      }
    } else {
      setTimeout(() => {
        this.handleMockResponse(action, payload, reqId);
      }, 50);
    }
  }

  private handleMockResponse(action: string, payload: Record<string, any>, requestId: string) {
    let resp: any = { action, requestId, ok: true };

    switch (action) {
      case 'list': {
        resp = {
          action: 'list',
          requestId,
          ok: true,
          runtimeEnabled: this.mockState.runtimeEnabled,
          showBounds: this.mockState.showBounds,
          activeWallpaperId: this.mockState.activeWallpaperId,
          activeWallpaperPackagePath: this.mockState.activeWallpaperPackagePath,
          activeWallpaperInteractive: this.mockState.activeWallpaperInteractive,
          userWidgetsDir: this.mockState.userWidgetsDir,
          logDir: this.mockState.logDir,
          isDebug: Boolean(import.meta.env.DEV),
          widgets: [...this.mockState.widgets],
          wallpapers: [...this.mockState.wallpapers]
        };
        break;
      }
      case 'launch':
      case 'start': {
        this.mockState.runtimeEnabled = true;
        const targetId = (payload.widgetId || '').toLowerCase();
        const w = this.mockState.widgets.find((item: any) => item.id.toLowerCase() === targetId);
        if (w) {
          w.running = true;
          w.status = 'running';
          if (!w.runningInstanceIds.length) {
            w.runningInstanceIds = [`installed:${w.id}`];
          }
        }
        resp = { action: 'launch', requestId, ok: true, instanceId: `installed:${targetId}` };
        break;
      }
      case 'stop':
      case 'unload': {
        const targetId = (payload.widgetId || '').toLowerCase();
        const w = this.mockState.widgets.find((item: any) => item.id.toLowerCase() === targetId);
        if (w) {
          w.running = false;
          w.status = 'stopped';
          w.runningInstanceIds = [];
        }
        resp = { action: 'stop', requestId, ok: true };
        break;
      }
      case 'restart': {
        resp = { action: 'restart', requestId, ok: true, restartedCount: 1 };
        break;
      }
      case 'set_permission': {
        const targetId = (payload.widgetId || '').toLowerCase();
        const perm = payload.permission;
        const granted = payload.granted;
        const w = this.mockState.widgets.find((item: any) => item.id.toLowerCase() === targetId);
        if (w) {
          if (granted && !w.grantedPermissions.includes(perm)) {
            w.grantedPermissions.push(perm);
          } else if (!granted) {
            w.grantedPermissions = w.grantedPermissions.filter((p: any) => p !== perm);
          }
        }
        resp = { action: 'set_permission', requestId, ok: true, grantedPermissions: w ? w.grantedPermissions : [] };
        break;
      }
      case 'save_widget_settings': {
        const targetId = (payload.widgetId || '').toLowerCase();
        const settings = payload.settings || {};
        const w = this.mockState.widgets.find((item: any) => item.id.toLowerCase() === targetId);
        if (w) {
          w.settingsValues = { ...(w.settingsValues || {}), ...settings };
        }
        resp = { action: 'save_widget_settings', requestId, ok: true, settings };
        break;
      }
      case 'clear_storage': {
        const targetId = (payload.widgetId || '').toLowerCase();
        const w = this.mockState.widgets.find((item: any) => item.id.toLowerCase() === targetId);
        if (w) {
          w.storageUsedBytes = 0;
        }
        resp = { action: 'clear_storage', requestId, ok: true, storageUsedBytes: 0 };
        break;
      }
      case 'set_master_enabled': {
        this.mockState.runtimeEnabled = !!payload.enabled;
        this.mockState.widgets.forEach((w: any) => {
          w.running = this.mockState.runtimeEnabled;
          w.status = this.mockState.runtimeEnabled ? 'running' : 'stopped';
        });
        resp = { action: 'set_master_enabled', requestId, ok: true, enabled: this.mockState.runtimeEnabled };
        break;
      }
      case 'set_show_bounds': {
        this.mockState.showBounds = !!payload.showBounds;
        resp = { action: 'set_show_bounds', requestId, ok: true, showBounds: this.mockState.showBounds };
        break;
      }
      case 'apply_wallpaper': {
        const wallId = payload.widgetId || 'lindesk.wallpaper';
        this.mockState.activeWallpaperId = wallId;
        resp = { action: 'apply_wallpaper', requestId, ok: true, activeWallpaperId: wallId };
        break;
      }
      case 'stop_wallpaper': {
        this.mockState.activeWallpaperId = '';
        resp = { action: 'stop_wallpaper', requestId, ok: true };
        break;
      }
      case 'set_titlebar_rects':
      case 'window_minimize':
      case 'window_maximize':
      case 'window_close':
      case 'window_drag': {
        resp = { action, requestId, ok: true };
        break;
      }
      case 'create_project': {
        const name = payload.name || '我的小组件';
        const newId = payload.id || `com.lindesk.dev${Date.now() % 10000}`;
        const newWidget: WidgetInfo = {
          id: newId,
          name,
          version: '1.0.0',
          author: payload.author || '本地开发者',
          description: payload.description || '新建的桌面小组件工程',
          role: 'widget',
          packagePath: `${this.mockState.userWidgetsDir}\\${name}`,
          source: '开发工程',
          isDevelopment: true,
          running: true,
          status: 'running',
          crashCount: 0,
          storageUsedBytes: 10240,
          declaredPermissions: ['storage', 'systemInfo'],
          grantedPermissions: ['storage', 'systemInfo'],
          hasSettings: false,
          runningInstanceIds: [`dev:${newId}`]
        };
        this.mockState.widgets.unshift(newWidget);
        resp = { action: 'create_project', requestId, ok: true, id: newId };
        break;
      }
      case 'uninstall': {
        const targetId = (payload.widgetId || '').toLowerCase();
        this.mockState.widgets = this.mockState.widgets.filter((item: any) => item.id.toLowerCase() !== targetId);
        resp = { action: 'uninstall', requestId, ok: true };
        break;
      }
      default: {
        resp = { action, requestId, ok: true };
      }
    }

    this.recordLog('in', action, resp, resp.ok);
    this.notifyListeners(resp);
  }
}

export const ipc = new IPCService();
