#ifndef WINBLURHELPER_H
#define WINBLURHELPER_H

#include <QWidget>

class WinBlurHelper {
public:
    enum BlurMode {
        None,
        Acrylic,
        Mica
    };

    static void applyBlur(QWidget* widget, BlurMode mode = Acrylic);
};

#endif // WINBLURHELPER_H
