#pragma once
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QList>
#include <QDir>
#include "ListItem.h"
#include <QUuid>
#include <QVector>

class DebugDialog : public QDialog {
public:
    DebugDialog(int iconCount, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Debug");
        QLabel* label = new QLabel(QString("1").arg(iconCount), this);
        label->setAlignment(Qt::AlignCenter);
        resize(200, 80);
    }
};


class LinDesk : public QWidget {
    Q_OBJECT

public:
    // 获取单例对象
    static LinDesk* instance(QWidget* parent = nullptr);

    void loadIconLayer();

    QVector<ListItem*> getAllItems() const;

private:
    explicit LinDesk(QWidget* parent = nullptr);
    ~LinDesk();

    QVector<ListItem*> m_listItems;

    static LinDesk* m_instance;
};
