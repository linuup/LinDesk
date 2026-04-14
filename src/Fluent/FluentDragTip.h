#pragma once

#include <QWidget>

class QLabel;

class FluentDragTip : public QWidget
{
    Q_OBJECT
public:
    explicit FluentDragTip(QWidget* parent = nullptr);

    void setText(const QString& text);
    void showAt(const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateMask();

    QLabel* m_label = nullptr;
};

