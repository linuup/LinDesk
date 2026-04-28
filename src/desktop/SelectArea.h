#pragma once

#include <QWidget>
#include <qlabel.h>
#include <QMouseEvent>
#include <QPainter>
#include <qdebug.h>
#include <QPointer>
class SelectArea : public QWidget
{
	Q_OBJECT

public:
	SelectArea(QWidget *parent = nullptr);
	~SelectArea();
    void setSelectionRect(const QRect& selectionRect);
    QRect selectionRect() const { return m_rect; }
    void syncToParent();
    void syncToRect(const QRect& rect);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect m_rect;
    QPointer<QWidget> m_hostWidget;
};
