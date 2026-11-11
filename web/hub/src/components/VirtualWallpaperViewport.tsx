import React, { useState, useEffect, useRef } from 'react';
import { WallpaperInfo } from '../types';

interface VirtualWallpaperViewportProps {
  wall: WallpaperInfo;
  src: string;
  onLoad?: () => void;
  iframeRef?: React.Ref<HTMLIFrameElement>;
  className?: string;
  filterStyle?: React.CSSProperties;
}

export const VirtualWallpaperViewport: React.FC<VirtualWallpaperViewportProps> = ({
  wall,
  src,
  onLoad,
  iframeRef,
  className = '',
  filterStyle = {},
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

  // Scale calculation: uniform scale preserving screen aspect ratio within the container
  const scale = dimensions.width > 0 && dimensions.height > 0
    ? Math.min(dimensions.width / screenW, dimensions.height / screenH)
    : 1;

  const scaledW = screenW * scale;
  const scaledH = screenH * scale;
  const leftOffset = Math.max(0, (dimensions.width - scaledW) / 2);
  const topOffset = Math.max(0, (dimensions.height - scaledH) / 2);

  return (
    <div
      ref={containerRef}
      className={`relative w-full h-full overflow-hidden bg-black select-none ${className}`}
    >
      {dimensions.width > 0 && dimensions.height > 0 && (
        <div
          style={{
            position: 'absolute',
            left: `${leftOffset}px`,
            top: `${topOffset}px`,
            width: `${screenW}px`,
            height: `${screenH}px`,
            transform: `scale(${scale})`,
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
