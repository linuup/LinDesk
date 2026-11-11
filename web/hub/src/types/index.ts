export interface WidgetInstanceItem {
  instanceId: string;
  running: boolean;
  visible: boolean;
  geometry: {
    x: number;
    y: number;
    width: number;
    height: number;
  };
}

export interface WidgetSettingOption {
  label: string;
  value: any;
}

export interface WidgetSettingField {
  key: string;
  type: 'select' | 'boolean' | 'string' | 'number' | 'color';
  title?: string;
  description?: string;
  default?: any;
  value?: any;
  options?: WidgetSettingOption[];
  min?: number;
  max?: number;
  step?: number;
}

export interface WidgetInfo {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  role: 'widget' | 'wallpaper' | string;
  packagePath: string;
  source: string;
  isDevelopment: boolean;
  trusted?: boolean;
  enabled?: boolean;
  running: boolean;
  status: 'running' | 'stopped' | 'crashed' | 'disabled' | string;
  crashCount: number;
  packageSizeBytes?: number;
  storageUsedBytes: number;
  storageQuotaBytes?: number;
  declaredPermissions: string[];
  grantedPermissions: string[];
  hasSettings: boolean;
  settingsFile?: string;
  settingsSchema?: WidgetSettingField[];
  settingsValues?: Record<string, any>;
  runningInstanceIds: string[];
  instances?: WidgetInstanceItem[];
}

export interface WallpaperPropertySchema {
  type: 'slider' | 'color' | 'boolean' | 'select' | 'string' | 'number';
  text?: string;
  min?: number;
  max?: number;
  step?: number;
  value?: any;
  options?: Array<{ label: string; value: any }>;
}

export interface CustomWallpaperConfig {
  scaling?: 'cover' | 'contain' | 'fill';
  dimming?: number;
  blur?: number;
  volume?: number;
  muted?: boolean;
  playbackRate?: number;
  audioDucking?: boolean;
}

export interface StagedWallpaperMedia {
  filePath: string;
  fileUrl: string;
  name: string;
  ext: string;
  wallpaperType: 'video' | 'image';
  sizeBytes?: number;
  editWallpaperId?: string;
  packagePath?: string;
  description?: string;
  sourcePath?: string;
  isArchived?: boolean;
  fileSizeBytes?: number;
  isMissing?: boolean;
  customConfig?: CustomWallpaperConfig;
  isNewlyStaged?: boolean;
}

export interface WallpaperInfo {
  id: string;
  name: string;
  version?: string;
  author?: string;
  description: string;
  packagePath: string;
  role: 'wallpaper';
  interactive?: boolean;
  running?: boolean;
  properties?: Record<string, WallpaperPropertySchema | any>;
  tag?: string;
  wallpaperType?: 'video' | 'image' | 'web' | string;
  isCustom?: boolean;
  mediaUrl?: string;
  previewUrl?: string;
  sourcePath?: string;
  isArchived?: boolean;
  fileSizeBytes?: number;
  isMissing?: boolean;
  customConfig?: CustomWallpaperConfig;
}

export interface ManagerState {
  runtimeEnabled: boolean;
  showBounds: boolean;
  activeWallpaperId: string;
  activeWallpaperPackagePath: string;
  activeWallpaperInteractive: boolean;
  userWidgetsDir: string;
  logDir: string;
  widgets: WidgetInfo[];
  wallpapers: WallpaperInfo[];
}

export interface UniversalWallpaperSettings {
  volume: number; // 0 - 100
  audioDucking: boolean; // 第三方发声时自动避让静音
  dimming: number; // 0 - 80 桌面图标易读性暗度遮罩
  blur: number; // 0 - 100 桌面背景高斯模糊百分比
  scaling: 'cover' | 'contain' | 'fill';
  playbackRate: number; // 0.5 - 2.0
  interactive: boolean;
  muted?: boolean;
}

export type MainTabType = 'overview' | 'studio' | 'widgets' | 'desktop';

export interface IPCMessageLog {
  id: string;
  timestamp: string;
  direction: 'in' | 'out';
  action: string;
  payload: any;
  ok?: boolean;
}

export interface SystemPaths {
  userWidgetsDir: string;
  userLogsDir: string;
  appDir?: string;
}

export interface CreateWidgetData {
  name: string;
  id: string;
  author: string;
  description: string;
  template: 'vanilla' | 'react' | 'webgl';
  width: number;
  height: number;
}
