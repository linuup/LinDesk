#include <QDialog>
#include <QLabel>
#include <QDebug>
// Debug窗体，显示调试信息
class DebugDialog : public QDialog {
public:
    DebugDialog(int iconCount, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Debug");
        QLabel* label = new QLabel(QString("图标数量：%1").arg(iconCount), this);
        label->setAlignment(Qt::AlignCenter);
        resize(200, 80);
    }
};

#include <QApplication>
#include <QWidget>
#include <QScreen>
#include <QDir>
#include <QFileInfoList>
#include <QStandardPaths>
#include <QMetaObject>
#include <QList>
#include "DeskFileModel.h"
#include "DeskFileUi.h"


// 主窗体，铺满整个windows视窗
class LinDesk : public QWidget {
public:
    LinDesk(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowTitle("LinDesk");
        // 全屏铺满
        setGeometry(QApplication::primaryScreen()->geometry());
        // 初始化文件模型
        initFileModels();
        // 创建UI并设置模型
        m_fileUis.reserve(m_fileModels.size());
        int iconSize = 80, padding = 30;
        for (int i = 0; i < m_fileModels.size(); ++i) {
            DeskFileUi* ui = new DeskFileUi(this); // 设置父控件为LinDesk
            ui->setModel(&m_fileModels[i]);
            int x = padding + (i % 6) * (iconSize + padding);
            int y = padding + (i / 6) * (iconSize + 40);
            ui->resize(iconSize, iconSize + 30);
            ui->move(x, y); // 相对父控件定位
            ui->show();
            m_fileUis.append(ui);
        }
        // 显示Debug窗体
        DebugDialog* debug = new DebugDialog(m_fileModels.size(), this);
        debug->show();
    }

private:
    QList<DeskFileModel> m_fileModels;
    QList<DeskFileUi*> m_fileUis;
    void initFileModels() {
        m_fileModels.clear();
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QDir dir(desktopPath);
        QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
        for (const QFileInfo& info : fileList) {
            m_fileModels.append(DeskFileModel(info.fileName(), info.absoluteFilePath(), ""));
        }
    }
};

#include "wil/com.h"
#include "OS.h"
using namespace OS_Module;
int main(int argc, char *argv[])
{
    printHi();
    QApplication app(argc, argv);
	qDebug() << "Application Dir:" << app.applicationDirPath();
    LinDesk w;
    w.show();
    return app.exec();
}
