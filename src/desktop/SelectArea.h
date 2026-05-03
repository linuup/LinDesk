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
    void loadColors();
    void applyColors();
    void updateVisuals();

    QRect m_rect;
    QPointer<QWidget> m_hostWidget;
    QColor m_fillColor;
    QColor m_borderColor;
    QPointer<QWidget> m_fillWidget;
    QPointer<QWidget> m_borderWidget;
};
