import React, { useState, useEffect, useRef } from 'react';
import { WallpaperInfo } from '../types';

interface VirtualWallpaperViewportProps {
  wall: WallpaperInfo;
  src: string;
  onLoad?: () => void;
  iframeRef?: React.Ref<HTMLIFrameElement>;
  className?: string;
  filterStyle?: React.CSSProperties;
  fitMode?: 'cover' | 'contain';
  enableAmbientBackdrop?: boolean;
}

export const VirtualWallpaperViewport: React.FC<VirtualWallpaperViewportProps> = ({
  wall,
  src,
  onLoad,
  iframeRef,
  className = '',
  filterStyle = {},
  fitMode = 'contain',
  enableAmbientBackdrop = true,
}) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [dimensions, setDimensions] = useState<{ width: number; height: number }>({ width: 0, height: 0 });

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    // Read initial dimensions
    const updateSize = () => {
      if (el.clientWidth > 0 && el.clientHeight > 0) {
        setDimensions({
          width: el.clientWidth,
          height: el.clientHeight,
        });
      }
    };

    updateSize();

    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        if (entry.contentRect && entry.contentRect.width > 0 && entry.contentRect.height > 0) {
          setDimensions({
            width: entry.contentRect.width,
            height: entry.contentRect.height,
          });
        }
      }
    });

    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  // Use the user's actual screen resolution (fallback to 1920x1080)
  const screenW = (typeof window !== 'undefined' && window.screen && window.screen.width > 0)
    ? window.screen.width
    : 1920;
  const screenH = (typeof window !== 'undefined' && window.screen && window.screen.height > 0)
    ? window.screen.height
    : 1080;

  // Contain scale (preserves exact screen ratio inside frame)
  const containScale = dimensions.width > 0 && dimensions.height > 0
    ? Math.min(dimensions.width / screenW, dimensions.height / screenH)
    : 1;

  // Cover scale (fills 100% of container with zero black edges)
  const coverScale = dimensions.width > 0 && dimensions.height > 0
    ? Math.max(dimensions.width / screenW, dimensions.height / screenH)
    : 1;

  const activeScale = fitMode === 'cover' ? coverScale : containScale;
  const leftOffset = (dimensions.width - screenW * activeScale) / 2;
  const topOffset = (dimensions.height - screenH * activeScale) / 2;

  // Background ambient backdrop scale (slightly oversized for soft blur)
  const backdropScale = coverScale * 1.15;
  const backdropLeft = (dimensions.width - screenW * backdropScale) / 2;
  const backdropTop = (dimensions.height - screenH * backdropScale) / 2;

  return (
    <div
      ref={containerRef}
      className={`relative w-full h-full overflow-hidden select-none ${className}`}
    >
      {/* 1. Ambient Dynamic Light Glow (Apple Music / Spotify Style Backdrop) */}
      {enableAmbientBackdrop && dimensions.width > 0 && dimensions.height > 0 && (
        <div className="absolute inset-0 overflow-hidden pointer-events-none select-none z-0">
          <div
            style={{
              position: 'absolute',
              left: `${backdropLeft}px`,
              top: `${backdropTop}px`,
              width: `${screenW}px`,
              height: `${screenH}px`,
              transform: `scale(${backdropScale})`,
              transformOrigin: 'top left',
              filter: 'blur(64px) saturate(180%) brightness(0.9)',
              opacity: 0.9,
            }}
          >
            <iframe
              src={src}
              title=""
              className="w-full h-full border-0 pointer-events-none"
              tabIndex={-1}
              scrolling="no"
            />
          </div>
          {/* Subtle frosted glass mist overlay to blend ambient lighting seamlessly */}
          <div className="absolute inset-0 bg-slate-950/20 backdrop-blur-md" />
        </div>
      )}

      {/* 2. Main High-Fidelity Wallpaper Viewport */}
      {dimensions.width > 0 && dimensions.height > 0 && (
        <div
          className="relative z-10"
          style={{
            position: 'absolute',
            left: `${leftOffset}px`,
            top: `${topOffset}px`,
            width: `${screenW}px`,
            height: `${screenH}px`,
            transform: `scale(${activeScale})`,
            transformOrigin: 'top left',
            pointerEvents: 'none',
            filter: filterStyle?.filter,
            opacity: filterStyle?.opacity,
          }}
        >
          <iframe
            ref={iframeRef}
            src={src}
            title={wall.name}
            onLoad={onLoad}
            className="w-full h-full border-0 pointer-events-none select-none"
            tabIndex={-1}
            scrolling="no"
          />
        </div>
      )}
    </div>
  );
};
