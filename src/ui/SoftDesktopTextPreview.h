#ifndef SOFTDESKTOPTEXTPREVIEW_H
#define SOFTDESKTOPTEXTPREVIEW_H

#include "ui/SoftDesktopTextRenderer.h"

#include <QWidget>

class SoftDesktopTextPreview : public QWidget
{
    Q_OBJECT
public:
    explicit SoftDesktopTextPreview(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setPreviewFont(const QFont& font);
    void setTextColor(const QColor& color);
    void setGlowColor(const QColor& color);
    void setRendererOptions(const SoftDesktopTextRenderer::Options& options);
    const SoftDesktopTextRenderer::Options& rendererOptions() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QFont m_font;
    SoftDesktopTextRenderer::Options m_options;
};

#endif
