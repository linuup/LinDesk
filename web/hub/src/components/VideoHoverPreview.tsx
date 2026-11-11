import React from 'react';
import { Film } from 'lucide-react';
import { VideoLiveCanvas, cleanVideoPath } from './VideoLiveCanvas';

interface VideoHoverPreviewProps {
  videoUrl: string;
  posterUrl?: string;
  isHovered: boolean;
  className?: string;
}

export const formatVideoUrl = (raw: string): string => {
  return cleanVideoPath(raw);
};

export const VideoHoverPreview: React.FC<VideoHoverPreviewProps> = ({
  videoUrl,
  posterUrl,
  isHovered,
  className = '',
}) => {
  return (
    <div className={`relative w-full h-full overflow-hidden select-none ${className}`}>
      {/* Dynamic Native Streaming Live Canvas + Instant Static Poster fallback */}
      <VideoLiveCanvas
        videoPath={videoUrl}
        posterUrl={posterUrl}
        isActive={isHovered}
        fps={20}
        objectFit="cover"
        className="w-full h-full"
      />

      {/* Physical screen bezel highlight and contrast vignette */}
      <div className="absolute inset-0 ring-1 ring-inset ring-white/10 dark:ring-white/5 pointer-events-none" />
      <div className="absolute inset-0 bg-gradient-to-t from-black/45 via-transparent to-transparent pointer-events-none" />

      {/* Bottom Video Status Badge */}
      <div className="absolute bottom-2.5 right-2.5 z-10 px-2 py-0.5 rounded-md bg-black/60 backdrop-blur-md text-[10px] text-white font-medium flex items-center gap-1.5 border border-white/15 opacity-80 group-hover:opacity-100 transition-opacity">
        <Film className={`w-3 h-3 ${isHovered ? 'text-emerald-400 animate-pulse' : 'text-indigo-400'}`} />
        <span>{isHovered ? '正在预览' : '视频壁纸'}</span>
      </div>
    </div>
  );
};
