#pragma once
#include <QWidget>
#include <QList>
#include <QString>
#include <QRect>
#include <QPainter>
#include "DeskFileModel.h"

// 只绘制桌面文件图标和名称，数据通过DeskFileModel传递
class DeskFileUi : public QWidget {
public:
    explicit DeskFileUi(QWidget* parent = nullptr);
    void setModel(DeskFileModel* model);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    DeskFileModel* m_model = nullptr;
};
