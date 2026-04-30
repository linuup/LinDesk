#include "desktop/SelectArea.h"
#include <QCoreApplication>
#include <QSettings>

SelectArea::SelectArea(QWidget* parent)
    : QWidget(parent)
    , m_hostWidget(parent)
    , m_fillWidget(new QWidget(this))
    , m_borderWidget(new QWidget(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);

    const QList<QPointer<QWidget>> widgets = {
        m_fillWidget,
        m_borderWidget
    };
    for (const QPointer<QWidget>& widget : widgets) {
        if (!widget) {
            continue;
        }
        widget->setAttribute(Qt::WA_TransparentForMouseEvents);
        widget->hide();
    }

    loadColors();
    if (m_hostWidget) {
        syncToRect(m_hostWidget->rect());
    }
    hide();
}

void SelectArea::setSelectionRect(const QRect& selectionRect)
{
    if (m_rect == selectionRect) {
        return;
    }

    const QRect dirty = m_rect.united(selectionRect);
    m_rect = selectionRect;
    updateVisuals();
    update(dirty.adjusted(-1, -1, 1, 1));
}

void SelectArea::clearAndHide()
{
    m_rect = QRect();

    if (m_fillWidget) {
        m_fillWidget->hide();
        m_fillWidget->setGeometry(QRect());
    }
    if (m_borderWidget) {
        m_borderWidget->hide();
        m_borderWidget->setGeometry(QRect());
    }

    hide();
    update();
}

void SelectArea::syncToParent()
{
    if (m_hostWidget) {
        syncToRect(m_hostWidget->rect());
    }
}

void SelectArea::syncToRect(const QRect& rect)
{
    if (geometry() == rect) {
        return;
    }
    setGeometry(rect);
}

void SelectArea::loadColors()
{
    QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    const QColor baseColor(settings.value("Desktop/SelectionRectangleColor", QStringLiteral("#64A0F0")).toString());
    QColor fillColor = baseColor.isValid() ? baseColor : QColor(100, 160, 240);
    QColor borderColor = fillColor.darker(110);
    fillColor.setAlpha(70);
    borderColor.setAlpha(180);
    m_fillColor = fillColor;
    m_borderColor = borderColor;
    applyColors();
}

void SelectArea::applyColors()
{
    if (m_fillWidget) {
        m_fillWidget->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_fillColor.name(QColor::HexArgb)));
    }
    if (m_borderWidget) {
        m_borderWidget->setStyleSheet(QStringLiteral(
            "background-color: transparent;"
            "border: 1px solid %1;").arg(m_borderColor.name(QColor::HexArgb)));
    }
}

void SelectArea::updateVisuals()
{
    if (!m_rect.isValid() || m_rect.isEmpty() || m_rect.width() <= 0 || m_rect.height() <= 0) {
        if (m_fillWidget) {
            m_fillWidget->hide();
        }
        if (m_borderWidget) {
            m_borderWidget->hide();
        }
        return;
    }

    const QRect outerRect = m_rect.normalized();
    const QRect innerRect = outerRect.adjusted(1, 1, -1, -1);

    if (m_fillWidget) {
        if (innerRect.isValid() && innerRect.width() > 0 && innerRect.height() > 0) {
            m_fillWidget->setGeometry(innerRect);
            m_fillWidget->show();
        } else {
            m_fillWidget->hide();
        }
    }

    if (m_borderWidget) {
        m_borderWidget->setGeometry(outerRect);
        m_borderWidget->show();
    }
}

SelectArea::~SelectArea()
{}
