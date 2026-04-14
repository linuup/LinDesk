#ifndef SOFTDESKTOPTEXTPREVIEW_H
#define SOFTDESKTOPTEXTPREVIEW_H

#include <QWidget>
#include <QColor>
#include <QFont>

class SoftDesktopTextPreview : public QWidget
{
    Q_OBJECT
public:
    explicit SoftDesktopTextPreview(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setPreviewFont(const QFont& font);
    void setTextColor(const QColor& color);
    void setGlowColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QFont m_font;
    QColor m_textColor;
    QColor m_glowColor;
};

#endif
