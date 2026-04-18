#include "ui/SoftDesktopTextPreview.h"
#include "ui/SoftDesktopTextRenderer.h"

#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>

SoftDesktopTextPreview::SoftDesktopTextPreview(QWidget* parent)
    : QWidget(parent),
      m_text("New Text Document.txt")
{
    m_font.setFamily("Segoe UI");
    m_font.setPixelSize(36);
    m_font.setWeight(QFont::Normal);
    m_options.textColor = QColor(255, 255, 255);
    m_options.glowColor = QColor(0, 0, 0, 210);
    m_options.innerShadowColor = QColor(0, 0, 0, 155);
    m_options.largeGlowRadius = 6;
    m_options.tightGlowRadius = 2;
    m_options.glowOffset = QPoint(0, 1);
    m_options.innerShadowOffset = QPoint(0, 1);
    m_options.lineSpacingAdjust = -2;
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void SoftDesktopTextPreview::setText(const QString& text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    update();
}

void SoftDesktopTextPreview::setPreviewFont(const QFont& font)
{
    m_font = font;
    update();
}

void SoftDesktopTextPreview::setTextColor(const QColor& color)
{
    m_options.textColor = color;
    update();
}

void SoftDesktopTextPreview::setGlowColor(const QColor& color)
{
    m_options.glowColor = color;
    update();
}

void SoftDesktopTextPreview::setRendererOptions(const SoftDesktopTextRenderer::Options& options)
{
    m_options = options;
    update();
}

const SoftDesktopTextRenderer::Options& SoftDesktopTextPreview::rendererOptions() const
{
    return m_options;
}

void SoftDesktopTextPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0.0, QColor(69, 88, 118));
    background.setColorAt(0.45, QColor(94, 114, 140));
    background.setColorAt(1.0, QColor(57, 76, 104));
    painter.fillRect(rect(), background);

    painter.fillRect(rect(), QColor(255, 255, 255, 18));

    const QRect previewRect = rect().adjusted(18, 18, -18, -18);
    const QRect sampleRect = previewRect.adjusted(12, 18, -12, -18);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 26));
    painter.drawRoundedRect(sampleRect, 20, 20);

    painter.setBrush(QColor(255, 255, 255, 14));
    painter.drawRoundedRect(sampleRect.adjusted(1, 1, -1, -1), 18, 18);

    const QRect textRect(sampleRect.adjusted(18, 34, -18, -34));
    SoftDesktopTextRenderer::drawText(&painter,
                                      textRect,
                                      QStringList{ m_text, "Preview Sample" },
                                      m_font,
                                      m_options);
}
