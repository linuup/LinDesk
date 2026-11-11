import React, { useRef, useEffect, useState, useMemo } from 'react';

interface VideoLiveCanvasProps {
  videoPath: string;
  posterUrl?: string;
  isActive: boolean;
  fps?: number;
  className?: string;
  style?: React.CSSProperties;
  objectFit?: 'cover' | 'contain' | 'fill';
}

export const cleanVideoPath = (raw: string): string => {
  if (!raw) return '';
  let clean = raw.replace(/\\/g, '/');
  if (clean.startsWith('file:///')) {
    clean = clean.slice(8);
  } else if (clean.startsWith('file://')) {
    clean = clean.slice(7);
  }
  return clean;
};

export const VideoLiveCanvas: React.FC<VideoLiveCanvasProps> = ({
  videoPath,
  posterUrl,
  isActive,
  fps = 24,
  className = '',
  style = {},
  objectFit = 'cover',
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [hasFirstFrame, setHasFirstFrame] = useState(false);
  const activeRef = useRef(isActive);
  activeRef.current = isActive;

  const targetPath = useMemo(() => cleanVideoPath(videoPath), [videoPath]);

  useEffect(() => {
    if (!isActive || !targetPath) {
      setHasFirstFrame(false);
      return;
    }

    let isCancelled = false;
    let timerId: ReturnType<typeof setTimeout> | null = null;
    const intervalMs = Math.max(33, Math.round(1000 / fps));

    const fetchNextFrame = () => {
      if (isCancelled || !activeRef.current) return;

      const img = new Image();
      img.onload = () => {
        if (isCancelled || !activeRef.current) return;

        const canvas = canvasRef.current;
        if (canvas) {
          const ctx = canvas.getContext('2d');
          if (ctx) {
            if (canvas.width !== img.naturalWidth || canvas.height !== img.naturalHeight) {
              canvas.width = img.naturalWidth || 960;
              canvas.height = img.naturalHeight || 540;
            }
            ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
            setHasFirstFrame(true);
          }
        }

        if (!isCancelled && activeRef.current) {
          timerId = setTimeout(fetchNextFrame, intervalMs);
        }
      };

      img.onerror = () => {
        if (!isCancelled && activeRef.current) {
          // Retry slightly later on error
          timerId = setTimeout(fetchNextFrame, 200);
        }
      };

      img.src = `http://linwidget.bridge/video-frame?path=${encodeURIComponent(targetPath)}&t=${Date.now()}`;
    };

    // Kick off immediate first frame
    fetchNextFrame();

    return () => {
      isCancelled = true;
      if (timerId) clearTimeout(timerId);
    };
  }, [isActive, targetPath, fps]);

  return (
    <div
      className={`relative w-full h-full overflow-hidden select-none ${className}`}
      style={style}
    >
      {/* 1. Underlying 1080p Static Poster (Zero blank frames, instant appearance) */}
      {posterUrl && (
        <img
          src={posterUrl}
          alt="poster"
          className="absolute inset-0 w-full h-full select-none"
          style={{ objectFit }}
          loading="eager"
        />
      )}

      {/* 2. Hardware-accelerated Atomic Canvas Viewport (Zero flicker, smooth 30 FPS stream) */}
      <canvas
        ref={canvasRef}
        className={`absolute inset-0 w-full h-full pointer-events-none select-none transition-opacity duration-200 ${
          isActive && hasFirstFrame ? 'opacity-100' : 'opacity-0'
        }`}
        style={{ objectFit }}
      />
    </div>
  );
};
