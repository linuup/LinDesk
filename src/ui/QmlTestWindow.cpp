#include "ui/QmlTestWindow.h"
#include "app/AppFonts.h"

#include <QLabel>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>

QmlTestWindow::QmlTestWindow(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(tr("QML Test"));
    resize(620, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Qt Quick Test Page"), this);
    title->setStyleSheet(QStringLiteral(
        "font-family: %1;"
        "font-size: 22px;"
        "font-weight: 600;"
        "color: #1f2937;"
    ).arg(AppFonts::uiCssFamily()));
    layout->addWidget(title);

    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(QColor("#f3f4f6"));
    m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/qml/QmlTestPage.qml")));
    layout->addWidget(m_quickWidget, 1);
}
