import React from 'react';

interface HubIconProps {
  className?: string;
  size?: number;
}

export const HubIcon: React.FC<HubIconProps> = ({ className = 'w-5 h-5', size = 20 }) => {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      viewBox="0 0 256 256"
      width={size}
      height={size}
      className={className}
      style={{ display: 'inline-block', verticalAlign: 'middle', flexShrink: 0 }}
    >
      <defs>
        <filter id="hubAmbientShadowReact" x="-30%" y="-30%" width="160%" height="160%">
          <feDropShadow dx="0" dy="8" stdDeviation="10" floodColor="#0369a1" floodOpacity="0.25" />
          <feDropShadow dx="0" dy="3" stdDeviation="4" floodColor="#000000" floodOpacity="0.18" />
        </filter>

        <filter id="foldShadowReact" x="-30%" y="-30%" width="160%" height="160%">
          <feDropShadow dx="-1" dy="3" stdDeviation="4" floodColor="#0f172a" floodOpacity="0.28" />
        </filter>

        <linearGradient id="gradCyanReact" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stopColor="#00f5ff" />
          <stop offset="40%" stopColor="#00a6ff" />
          <stop offset="80%" stopColor="#2563eb" />
          <stop offset="100%" stopColor="#4f46e5" />
        </linearGradient>

        <linearGradient id="gradVioletReact" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stopColor="#818cf8" />
          <stop offset="40%" stopColor="#7c3aed" />
          <stop offset="80%" stopColor="#c026d3" />
          <stop offset="100%" stopColor="#e11d48" />
        </linearGradient>

        <linearGradient id="gradCoralReact" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stopColor="#fb7185" />
          <stop offset="40%" stopColor="#f43f5e" />
          <stop offset="80%" stopColor="#f59e0b" />
          <stop offset="100%" stopColor="#fbbf24" />
        </linearGradient>

        <linearGradient id="gradLimeReact" x1="0%" y1="100%" x2="100%" y2="0%">
          <stop offset="0%" stopColor="#facc15" />
          <stop offset="35%" stopColor="#34d399" />
          <stop offset="70%" stopColor="#06b6d4" />
          <stop offset="100%" stopColor="#00f5ff" />
        </linearGradient>

        <linearGradient id="rimGleamReact" x1="0%" y1="0%" x2="0%" y2="100%">
          <stop offset="0%" stopColor="#ffffff" stopOpacity="0.85" />
          <stop offset="100%" stopColor="#ffffff" stopOpacity="0.0" />
        </linearGradient>

        <radialGradient id="hubCoreAuraReact" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stopColor="#ffffff" stopOpacity="0.95" />
          <stop offset="35%" stopColor="#67e8f9" stopOpacity="0.6" />
          <stop offset="70%" stopColor="#818cf8" stopOpacity="0.2" />
          <stop offset="100%" stopColor="#ffffff" stopOpacity="0.0" />
        </radialGradient>
      </defs>

      <g filter="url(#hubAmbientShadowReact)">
        {/* Central soft ambient glow */}
        <circle cx="128" cy="128" r="62" fill="url(#hubCoreAuraReact)" />

        {/* 1. Top-Left Arc (Cyan to Royal Blue) */}
        <path
          d="M 128 26
             C 184 26, 230 72, 230 128
             C 230 152, 222 174, 206 190
             C 192 172, 186 148, 186 128
             C 186 96, 160 70, 128 70
             C 96 70, 70 96, 70 128
             C 70 148, 64 172, 50 190
             C 34 174, 26 152, 26 128
             C 26 72, 72 26, 128 26
             Z"
          fill="url(#gradCyanReact)"
        />

        {/* 2. Right Swirl Wing (Violet to Magenta) */}
        <path
          d="M 230 128
             C 230 184, 184 230, 128 230
             C 104 230, 82 222, 66 206
             C 84 192, 108 186, 128 186
             C 160 186, 186 160, 186 128
             C 186 96, 210 72, 230 128
             Z"
          fill="url(#gradVioletReact)"
          filter="url(#foldShadowReact)"
        />

        {/* 3. Bottom Swirl Wing (Coral to Amber Sun) */}
        <path
          d="M 128 230
             C 72 230, 26 184, 26 128
             C 26 104, 34 82, 50 66
             C 64 84, 70 108, 70 128
             C 70 160, 96 186, 128 186
             C 160 186, 184 210, 128 230
             Z"
          fill="url(#gradCoralReact)"
          filter="url(#foldShadowReact)"
        />

        {/* 4. Left Swirl Wing (Emerald to Bright Cyan) */}
        <path
          d="M 26 128
             C 26 72, 72 26, 128 26
             C 152 26, 174 34, 190 50
             C 172 64, 148 70, 128 70
             C 96 70, 70 96, 70 128
             C 70 160, 46 184, 26 128
             Z"
          fill="url(#gradLimeReact)"
          filter="url(#foldShadowReact)"
        />

        {/* Specular Rim Highlights */}
        <path
          d="M 128 30 C 180 30, 224 74, 226 126"
          fill="none"
          stroke="url(#rimGleamReact)"
          strokeWidth="3"
          strokeLinecap="round"
          opacity="0.8"
        />
        <path
          d="M 30 128 C 30 76, 74 32, 126 30"
          fill="none"
          stroke="url(#rimGleamReact)"
          strokeWidth="3"
          strokeLinecap="round"
          opacity="0.9"
        />
        <path
          d="M 74 128 C 74 98, 98 74, 128 74"
          fill="none"
          stroke="url(#rimGleamReact)"
          strokeWidth="2.5"
          strokeLinecap="round"
          opacity="0.6"
        />

        {/* Center Floating Hub Nexus ✦ */}
        <g transform="translate(128, 128)">
          <circle cx="0" cy="0" r="26" fill="#ffffff" opacity="0.4" />
          <circle cx="0" cy="0" r="18" fill="#67e8f9" opacity="0.6" />

          <path
            d="M 0 -26
               C 0 -7, 7 0, 26 0
               C 7 0, 0 7, 0 26
               C 0 7, -7 0, -26 0
               C -7 0, 0 -7, 0 -26
               Z"
            fill="#ffffff"
          />
          <path
            d="M 0 -11
               C 0 -3, 3 0, 11 0
               C 3 0, 0 3, 0 11
               C 0 3, -3 0, -11 0
               C -3 0, 0 -3, 0 -11
               Z"
            fill="#ffffff"
            transform="rotate(45)"
            opacity="0.8"
          />
          <circle cx="0" cy="0" r="4" fill="#0284c7" />
          <circle cx="0" cy="0" r="2.2" fill="#ffffff" />
        </g>
      </g>
    </svg>
  );
};

export default HubIcon;
