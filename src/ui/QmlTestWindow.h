#pragma once

#include <QWidget>

class QQuickWidget;

class QmlTestWindow : public QWidget
{
    Q_OBJECT

public:
    explicit QmlTestWindow(QWidget* parent = nullptr);

private:
    QQuickWidget* m_quickWidget = nullptr;
};
