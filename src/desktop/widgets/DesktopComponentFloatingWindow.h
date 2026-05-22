#pragma once

#include "desktop/widgets/DesktopComponentItem.h"

#include <QWidget>

class DesktopComponentFloatingWindow : public QWidget {
    Q_OBJECT

public:
    explicit DesktopComponentFloatingWindow(QWidget* parent = nullptr);

    QString componentId() const { return m_item.id; }
    void setComponent(const DesktopComponentItemModel& item, qreal dpiScale);

signals:
    void startPauseRequested(const QString& componentId);
    void wheelAdjustRequested(const QString& componentId, int deltaMinutes);
    void contextMenuRequested(const QString& componentId, const QPoint& globalPos);
    void movedRequested(const QString& componentId, const QPoint& globalTopLeft);
    void moveFinished(const QString& componentId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    DesktopComponentItemModel m_item;
    qreal m_dpiScale = 1.0;
    bool m_dragging = false;
    QPoint m_dragStartGlobalPos;
    QPoint m_dragStartWindowPos;
};
