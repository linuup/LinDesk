#pragma once
#include "app/LinDesk.h"
#include <QDebug>
#include <SystemApi.h>
#include <ShellContextMenu.h>
#include <QContextMenuEvent>

// 静态成员初始化
LinDesk* LinDesk::m_instance = nullptr;

LinDesk::LinDesk(QWidget* parent) : QWidget(parent) {
    // 设置桌面背景图片
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    setWindowTitle("LinDesk");
    setObjectName("LinDesk");
    // 设置无边框窗口
    //setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 设置透明背景
    //setAttribute(Qt::WA_TranslucentBackground);

    // 设置透明并允许透过窗口看到后面的内容
    setAttribute(Qt::WA_TransparentForMouseEvents); // 允许点击透过窗口
    setAttribute(Qt::WA_OpaquePaintEvent);  // 启用背景透明化

    setGeometry(600,600,600,500);

}

LinDesk::~LinDesk() {
    // 清理 ListItem 对象
    //qDeleteAll(m_listItems);
}

void LinDesk::contextMenuEvent(QContextMenuEvent* event)
{
    
}

LinDesk* LinDesk::instance(QWidget* parent) {
    // 如果实例不存在，则创建新的实例
    if (!m_instance) {
        m_instance = new LinDesk(parent);
    }
    return m_instance;
}
