#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include <QUuid>
#include <QVector>
#include <QSharedPointer>
#include "desktop/RightMenu.h"

class LinDesk : public QWidget {
    Q_OBJECT

public:
    // 获取单例对象
    static LinDesk* instance(QWidget* parent = nullptr);
	
private:
    explicit LinDesk(QWidget* parent = nullptr);
    ~LinDesk();

    static LinDesk* m_instance;


protected:
	void contextMenuEvent(QContextMenuEvent* event) override;

    
};
