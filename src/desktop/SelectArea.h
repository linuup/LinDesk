#pragma once

#include <QWidget>
#include <qlabel.h>
#include <QMouseEvent>
#include <QPainter>
#include <qdebug.h>
class SelectArea : public QWidget
{
	Q_OBJECT

public:
	SelectArea(QWidget *parent = nullptr);
	~SelectArea();
    void setSelectionRect(const QRect& selectionRect);
    QRect selectionRect() const { return m_rect; }
    void syncToParent();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect m_rect;
};
