#include "DeskFileUi.h"
#include <QPaintEvent>

DeskFileUi::DeskFileUi(QWidget* parent)
    : QWidget(parent), m_model(nullptr) {}

void DeskFileUi::setModel(DeskFileModel* model) {
    m_model = model;
    update();
}

void DeskFileUi::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    if (!m_model) return;
    int iconSize = 64;
    int padding = 8;
    // 绘制图标（此处用矩形代替）
    painter.drawRect(padding, padding, iconSize, iconSize);
    // 绘制文件名
    painter.drawText(padding, padding + iconSize + 15, m_model->name());
}
