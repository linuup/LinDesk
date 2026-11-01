#ifndef LINHUBWEBWIDGET_H
#define LINHUBWEBWIDGET_H

#include <QWidget>
#include <QImage>
#include <QString>

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
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    LinCef::CefOsrBrowserInstance* m_browser = nullptr;
    QImage m_frame;
    QString m_url;
};

#endif // LINHUBWEBWIDGET_H
