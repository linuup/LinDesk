#include "WinBlurHelper.h"
#include <windows.h>
#include <dwmapi.h>
#include <QLibrary>
#include <QDebug>

// Windows undocumented enum/struct for SetWindowCompositionAttribute (Acrylic on Win10/11)
enum WindowCompositionAttribute {
    WCA_ACCENT_POLICY = 19
};

enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
};

struct AccentPolicy {
    AccentState AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WindowCompositionAttributeData {
    WindowCompositionAttribute Attribute;
    PVOID Data;
    SIZE_T SizeOfData;
};

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WindowCompositionAttributeData*);

void WinBlurHelper::applyBlur(QWidget* widget, BlurMode mode) {
    if (!widget) return;
    HWND hwnd = (HWND)widget->winId();
    if (!hwnd) return;

    // 1. DWM Attributes - Full window backdrop region
    MARGINS margins = {-1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE

    // 2. Windows 11 Official Backdrop (Modern API)
    // 38 is DWMWA_SYSTEMBACKDROP_TYPE
    // 1=None, 2=Mica, 3=Acrylic, 4=MicaAlt
    DWORD backdropType = (mode == Acrylic) ? 3 : 1;
    DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));

    // 3. SetWindowCompositionAttribute (The standard cross-version blur engine)
    QLibrary user32("user32.dll");
    pfnSetWindowCompositionAttribute setWindowCompositionAttribute = 
        (pfnSetWindowCompositionAttribute)user32.resolve("SetWindowCompositionAttribute");
    
    if (setWindowCompositionAttribute) {
        AccentPolicy accent = { ACCENT_DISABLED, 0, 0, 0 };
        
        if (mode == Acrylic) {
            accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            accent.AccentFlags = 2; // Required
            // 0x01000000 (ARGB: 1/255 Alpha, 0 RGB):
            // This tells DWM there is a surface at 1/255 transparency. 
            // It allows the Hit-Test to succeed for clicks without being snapped to solid black.
            accent.GradientColor = 0x01000000; 
        } else {
            accent.AccentState = ACCENT_DISABLED;
        }

        WindowCompositionAttributeData data;
        data.Attribute = WCA_ACCENT_POLICY;
        data.Data = &accent;
        data.SizeOfData = sizeof(accent);

        setWindowCompositionAttribute(hwnd, &data);
    }
}
