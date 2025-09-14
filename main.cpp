#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QDebug>

class HelloWidget : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont("Arial", 24));
        painter.drawText(rect(), Qt::AlignCenter, "Hello Qt");
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    HelloWidget w;
    w.resize(400, 200);
    w.setWindowTitle("Hello Qt");
    w.show();
    return app.exec();
}
