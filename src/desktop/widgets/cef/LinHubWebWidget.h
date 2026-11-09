#ifndef LINHUBWEBWIDGET_H
#define LINHUBWEBWIDGET_H

#include <QWidget>
#include <QImage>
#include <QString>

class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

namespace LinCef {
class CefOsrBrowserInstance;
}

class LinHubWebWidget : public QWidget {
    Q_OBJECT

public:
    explicit LinHubWebWidget(const QString& url, QWidget* parent = nullptr);
    ~LinHubWebWidget() override;

    void startBrowser();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool handleDragEnter(QDragEnterEvent* event);
    bool handleDragMove(QDragMoveEvent* event);
    bool handleDragLeave(QDragLeaveEvent* event);
    bool handleDrop(QDropEvent* event);
    void notifyDragState(bool dragging);

    LinCef::CefOsrBrowserInstance* m_browser = nullptr;
    QImage m_frame;
    QString m_url;
    bool m_isDraggingOver = false;
};

#endif // LINHUBWEBWIDGET_H
