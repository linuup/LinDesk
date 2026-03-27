#ifndef SETTINGUI_GL_H
#define SETTINGUI_GL_H

#include "Fluent/FluentBaseWidget.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QVector>

class LinBox;
class QStackedWidget;
class QListWidget;

/**
 * @brief An OpenGL-accelerated version of the Settings UI.
 * This class uses a QOpenGLWidget as the central background area to support 
 * high-performance visual previews (like wallpaper shaders or 3D effects) 
 * while maintaining the Fluent Design structure.
 */
class SettingUI_GL : public FluentBaseWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit SettingUI_GL(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent = nullptr);
    void setTargetBox(LinBox* target);

protected:
    void closeEvent(QCloseEvent* event) override;
    
    // Potential OpenGL support if we want to draw something in the background
    // void initializeGL();
    // void paintGL();
    // void resizeGL(int w, int h);
    
private:
    void initUI();
    void setupStyles();
    
    // Page Creation Helpers (Consistent with the standard UI)
    QWidget* createGeneralPage();
    QWidget* createDesktopPage();
    QWidget* createOpenGLPage(); // Specialized GL settings
    QWidget* createPluginsPage();
    QWidget* createAboutPage();

    LinBox* m_targetBox;
    QVector<LinBox*> m_allBoxes;
    
    QListWidget* m_sidebar;
    QStackedWidget* m_contentStack;
};

#endif // SETTINGUI_GL_H
