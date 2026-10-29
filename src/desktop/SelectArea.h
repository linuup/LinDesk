#pragma once

#include <QWidget>
#include <QColor>
#include <QPointer>

class SelectArea : public QWidget
{
	Q_OBJECT

public:
	SelectArea(QWidget *parent = nullptr);
	~SelectArea();
    void setSelectionRect(const QRect& selectionRect);
    QRect selectionRect() const { return m_rect; }
    void clearAndHide();
    void syncToParent();
    void syncToRect(const QRect& rect);
    void reloadColorsFromSettings();

private:
    QRect m_rect;
    QPointer<QWidget> m_hostWidget;
};
